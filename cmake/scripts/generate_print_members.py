#!/usr/bin/env python3
# generate_print_members.py -- emit print_members definitions, split by namespace.
#
# Reads the JSON tags file from goal 03 ($BUILDDIR/generated/ctags/tags.json) in
# a single pass and writes one print_members.cpp per namespace bucket under
# <output_dir>/<bucket>/print_members.cpp.
#
# Which types are generated is read straight from tags.json: the ctags call in
# goal 03 passes `-D AVA_DEBUG_PRINT_MEMBERS_ON=void print_members_opt_in() { }`, so
# every class/struct that opted in (has that macro in its body) shows a
# `print_members_opt_in` member whose scope is that type's fully-qualified name. A
# generated definition only compiles for a type that declared print_members_opt_in, so
# only those types are emitted.
#
# Bucketing (collapse rule): a type in ava::<submodule>[::...] goes to bucket
# ava/<submodule>; any other top-level namespace gets its own bucket. Everything
# deeper folds into its bucket. The generated body is uniform (`<< member`); how
# each member is printed is resolved by C++ overload/concept dispatch at compile
# time, so member types are not needed here.
#
# Usage: generate_print_members.py <tags.json> <output_dir>
import json
import os
import sys
from collections import defaultdict


def label(member_name):
    # Member variables use a trailing-underscore convention; the printed label
    # drops a single trailing underscore (options_ -> "options:").
    return member_name[:-1] if member_name.endswith("_") else member_name


def bucket_for(scope):
    # Returns (bucket_namespace, bucket_dir). ava:: types collapse to their
    # first-level submodule; anything else uses its top-level namespace.
    if scope.startswith("ava::"):
        second = scope.split("::")[1]
        return ("ava::" + second, "ava/" + second)
    top = scope.split("::")[0]
    return (top, top)


def rel_qname(fqname, bucket_ns):
    # The print_members definition is emitted inside the bucket's namespace
    # block, so qualify the type relative to that namespace.
    prefix = bucket_ns + "::"
    return fqname[len(prefix):] if fqname.startswith(prefix) else fqname


def load_tags(tags_path):
    classes = {}                       # fqname -> class/struct tag (name, scope, path)
    members_by_scope = defaultdict(list)  # fqname -> [(line, member_name)]
    opted = set()                      # fqnames that declare print_members_opt_in
    with open(tags_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            tag = json.loads(line)
            kind = tag.get("kind")
            name = tag.get("name")
            scope = tag.get("scope") or ""
            fqname = (scope + "::" + name) if (scope and name) else None
            if kind in ("class", "struct") and name and tag.get("path") and fqname:
                classes[fqname] = tag
            elif kind == "member" and scope and name:
                # Keep the line so members can be emitted in declaration order
                # rather than tags.json's alphabetical order.
                members_by_scope[scope].append((tag.get("line", 0), name))
            if name == "print_members_opt_in" and scope:
                # Opt-in marker: the enclosing type declares print_members_opt_in.
                opted.add(scope)
    return classes, members_by_scope, opted


def emit_body(out, rel, members):
    out.write("void " + rel + "::print_members(std::ostream& os, char const* prefix) const\n")
    out.write("{\n")
    out.write("  os << prefix;\n")
    out.write("  LIBCWD_USING_OSTREAM_PRELUDE\n")
    out.write("  os << std::boolalpha\n")
    if not members:
        out.write("      ;\n")
    else:
        for idx, m in enumerate(members):
            sep = "" if idx == 0 else ", "
            out.write('     << __write__("' + sep + label(m) + ':") << ' + m + "\n")
        out.write("     ;\n")
    out.write("}\n\n")


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: generate_print_members.py <tags.json> <output_dir>\n")
        return 2
    tags_path, out_dir = argv[1:2 + 1]

    classes, members_by_scope, opted = load_tags(tags_path)

    buckets = defaultdict(list)  # (bucket_ns, bucket_dir) -> [(rel, members, header)]
    for fqname in opted:
        cls = classes.get(fqname)
        if not cls:
            continue
        path = cls.get("path", "")
        # Only generate for header-defined types: a type defined in a .cpp (for
        # example a pimpl) cannot be referenced from a separate generated
        # translation unit, so its print_members must stay hand-written.
        if not path.endswith((".h", ".hpp", ".hxx", ".hh")):
            continue
        name = cls["name"]
        scope = cls.get("scope") or ""
        members = [n for _, n in sorted(members_by_scope.get(fqname, []))]
        header = cls.get("path", "")
        header_rel = header[4:] if header.startswith("src/") else header
        bucket_ns, bucket_dir = bucket_for(scope)
        rel = rel_qname(fqname, bucket_ns)
        buckets[(bucket_ns, bucket_dir)].append((rel, members, header_rel))

    output_files = os.path.join(out_dir, "source_files");
    with open(output_files, "w") as out_files:
        for (bucket_ns, bucket_dir), types in sorted(buckets.items()):
            rel_path = os.path.join(bucket_dir, "print_members.cpp")
            out_files.write(rel_path + "\n");
            out_path = os.path.join(out_dir, rel_path)
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            headers = sorted({h for _, _, h in types})
            with open(out_path, "w") as out:
                out.write("// Auto-generated by generate_print_members.py. Do not edit.\n")
                out.write("// print_members definitions for namespace " + bucket_ns + ".\n\n")
                out.write('#include "sys.h"\n')
                for h in headers:
                    out.write('#include "' + h + '"\n')
                out.write('#include "ava/debug/debug_ostream_operators.h"\n\n')
                for ns in bucket_ns.split("::"):
                    out.write("namespace " + ns + " {\n")
                out.write("\n")
                for rel, members, _ in sorted(types, key=lambda t: t[0]):
                    emit_body(out, rel, members)
                for ns in reversed(bucket_ns.split("::")):
                    out.write("} // namespace " + ns + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
