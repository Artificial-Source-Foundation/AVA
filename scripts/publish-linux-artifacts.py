#!/usr/bin/env python3
"""Descriptor-safe input snapshots and no-replace Linux artifact publication."""

from __future__ import annotations

import argparse
import ctypes
import errno
import os
import pathlib
import secrets
import signal
import stat
import sys
from typing import NoReturn

RENAME_NOREPLACE = 1
DirectoryIdentity = tuple[int, int]
ExecutableIdentity = tuple[int, int, int, int, int]


class PublicationCancelled(BaseException):
    def __init__(self, signal_number: int) -> None:
        self.signal_number = signal_number
        super().__init__(f"publication cancelled by {signal.Signals(signal_number).name}")


def fail(message: str) -> NoReturn:
    raise RuntimeError(message)


def identity_of(status: os.stat_result) -> DirectoryIdentity:
    return status.st_dev, status.st_ino


def executable_identity_of(status: os.stat_result) -> ExecutableIdentity:
    return (
        status.st_dev,
        status.st_ino,
        status.st_size,
        status.st_mtime_ns,
        status.st_ctime_ns,
    )


def format_identity(identity: DirectoryIdentity) -> str:
    return f"{identity[0]}:{identity[1]}"


def parse_identity(value: str) -> DirectoryIdentity:
    parts = value.split(":")
    if len(parts) != 2:
        fail(f"invalid expected directory identity: {value!r}")
    try:
        device, inode = (int(part, 10) for part in parts)
    except ValueError:
        fail(f"invalid expected directory identity: {value!r}")
    if device < 0 or inode < 0:
        fail(f"invalid expected directory identity: {value!r}")
    return device, inode


def open_output_directory(
    path: pathlib.Path,
    expected_identity: DirectoryIdentity | None = None,
) -> tuple[int, os.stat_result]:
    try:
        listed = path.lstat()
    except FileNotFoundError:
        fail(f"output directory does not exist: {path}")
    if stat.S_ISLNK(listed.st_mode):
        fail(f"output directory must not be a symlink: {path}")
    if not stat.S_ISDIR(listed.st_mode):
        fail(f"output path is not a directory: {path}")

    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
    fd = os.open(path, flags)
    opened = os.fstat(fd)
    if opened.st_uid != os.geteuid():
        os.close(fd)
        fail(f"output directory is not owned by effective user {os.geteuid()}: {path}")
    if stat.S_IMODE(opened.st_mode) != 0o700:
        os.close(fd)
        fail(f"output directory must have exact mode 0700: {path}")
    if identity_of(listed) != identity_of(opened):
        os.close(fd)
        fail(f"output directory changed while opening: {path}")
    if expected_identity is not None and identity_of(opened) != expected_identity:
        actual = format_identity(identity_of(opened))
        expected = format_identity(expected_identity)
        os.close(fd)
        fail(
            f"output directory identity changed since approval: {path} "
            f"(expected {expected}, opened {actual})"
        )
    try:
        output_entries = os.listdir(fd)
    except OSError:
        os.close(fd)
        raise
    if output_entries:
        os.close(fd)
        fail(f"output directory must be empty before publication: {path}")
    return fd, opened


def revalidate_namespace(path: pathlib.Path, fd: int, initial: os.stat_result) -> None:
    try:
        current = path.stat(follow_symlinks=False)
    except OSError as exc:
        fail(f"output directory namespace changed: {path}: {exc}")
    opened = os.fstat(fd)
    expected = identity_of(initial)
    if identity_of(current) != expected or identity_of(opened) != expected:
        fail(f"output directory identity changed during publication: {path}")
    if not stat.S_ISDIR(current.st_mode) or stat.S_ISLNK(current.st_mode):
        fail(f"output directory namespace no longer identifies a directory: {path}")


def require_safe_name(name: str) -> None:
    if not name or name in {".", ".."} or pathlib.PurePath(name).name != name or "/" in name or "\0" in name:
        fail(f"unsafe artifact publication name: {name!r}")


def copy_file_descriptors(source_fd: int, target_fd: int, source: pathlib.Path) -> None:
    while True:
        chunk = os.read(source_fd, 1024 * 1024)
        if not chunk:
            break
        view = memoryview(chunk)
        while view:
            written = os.write(target_fd, view)
            if written <= 0:
                fail(f"short write while copying {source}")
            view = view[written:]


def is_executable_by_effective_user(status: os.stat_result) -> bool:
    mode = status.st_mode
    effective_uid = os.geteuid()
    if effective_uid == 0:
        return bool(mode & 0o111)
    if status.st_uid == effective_uid:
        return bool(mode & stat.S_IXUSR)
    if status.st_gid == os.getegid() or status.st_gid in os.getgroups():
        return bool(mode & stat.S_IXGRP)
    return bool(mode & stat.S_IXOTH)


def open_snapshot_source(source: pathlib.Path) -> tuple[int, os.stat_result]:
    try:
        source_fd = os.open(source, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    except OSError as exc:
        if exc.errno == errno.ELOOP:
            fail(f"snapshot source must not be a symlink: {source}")
        fail(f"unable to open executable snapshot source {source}: {exc}")
    source_status = os.fstat(source_fd)
    if not stat.S_ISREG(source_status.st_mode):
        os.close(source_fd)
        fail(f"snapshot source must name an executable regular file: {source}")
    if not is_executable_by_effective_user(source_status):
        os.close(source_fd)
        fail(f"snapshot source must name an executable regular file: {source}")
    return source_fd, source_status


def open_snapshot_directory(path: pathlib.Path) -> int:
    try:
        listed = path.lstat()
    except FileNotFoundError:
        fail(f"snapshot destination directory does not exist: {path}")
    if stat.S_ISLNK(listed.st_mode) or not stat.S_ISDIR(listed.st_mode):
        fail(f"snapshot destination parent must be a non-symlink directory: {path}")
    directory_fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
    opened = os.fstat(directory_fd)
    if identity_of(listed) != identity_of(opened):
        os.close(directory_fd)
        fail(f"snapshot destination directory changed while opening: {path}")
    if opened.st_uid != os.geteuid() or stat.S_IMODE(opened.st_mode) != 0o700:
        os.close(directory_fd)
        fail(f"snapshot destination directory must be owned by the effective user with mode 0700: {path}")
    return directory_fd


def snapshot_executable(source: pathlib.Path, destination: pathlib.Path) -> None:
    if destination.name in {"", ".", ".."}:
        fail(f"unsafe executable snapshot destination: {destination}")

    source_fd, initial_source_status = open_snapshot_source(source)
    directory_fd = -1
    target_fd = -1
    target_created = False
    try:
        directory_fd = open_snapshot_directory(destination.parent)
        target_fd = os.open(
            destination.name,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o700,
            dir_fd=directory_fd,
        )
        target_created = True
        copy_file_descriptors(source_fd, target_fd, source)
        final_source_status = os.fstat(source_fd)
        if executable_identity_of(final_source_status) != executable_identity_of(initial_source_status):
            fail(f"executable snapshot source changed while copying: {source}")
        os.fchmod(target_fd, 0o700)
        os.fsync(target_fd)
        os.close(target_fd)
        target_fd = -1
        os.fsync(directory_fd)
    except Exception:
        if target_fd >= 0:
            os.close(target_fd)
            target_fd = -1
        if target_created and directory_fd >= 0:
            try:
                os.unlink(destination.name, dir_fd=directory_fd)
            except OSError:
                pass
            try:
                os.fsync(directory_fd)
            except OSError:
                pass
        raise
    finally:
        if target_fd >= 0:
            os.close(target_fd)
        if directory_fd >= 0:
            os.close(directory_fd)
        os.close(source_fd)


def destination_absent(directory_fd: int, name: str) -> None:
    try:
        os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
    except FileNotFoundError:
        return
    fail(f"refusing to overwrite existing artifact or symlink: {name}")


def copy_to_temporary(
    directory_fd: int,
    source: pathlib.Path,
    temporary_name: str,
    temporary_files: list[tuple[str, DirectoryIdentity]],
) -> DirectoryIdentity:
    source_fd = os.open(source, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    target_fd = -1
    target_created = False
    try:
        source_status = os.fstat(source_fd)
        if not stat.S_ISREG(source_status.st_mode):
            fail(f"publication source is not a regular file: {source}")
        target_fd = os.open(
            temporary_name,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o600,
            dir_fd=directory_fd,
        )
        target_created = True
        copy_file_descriptors(source_fd, target_fd, source)
        os.fchmod(target_fd, 0o644)
        os.fsync(target_fd)
        temporary_identity = identity_of(os.fstat(target_fd))
        temporary_files.append((temporary_name, temporary_identity))
        return temporary_identity
    except BaseException as exc:
        if target_fd >= 0:
            os.close(target_fd)
            target_fd = -1
        if target_created:
            try:
                os.unlink(temporary_name, dir_fd=directory_fd)
            except OSError as cleanup_exc:
                raise RuntimeError(
                    f"{exc}; could not remove failed publication temporary {temporary_name}: {cleanup_exc}"
                ) from exc
        raise
    finally:
        if target_fd >= 0:
            os.close(target_fd)
        os.close(source_fd)


def rename_no_replace(directory_fd: int, temporary_name: str, final_name: str) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    renameat2 = getattr(libc, "renameat2", None)
    if renameat2 is None:
        fail("Linux renameat2 is required for atomic no-replace artifact publication")
    renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    renameat2.restype = ctypes.c_int
    result = renameat2(
        directory_fd,
        os.fsencode(temporary_name),
        directory_fd,
        os.fsencode(final_name),
        RENAME_NOREPLACE,
    )
    if result != 0:
        error_number = ctypes.get_errno()
        if error_number == errno.EEXIST:
            fail(f"refusing to overwrite existing artifact or symlink: {final_name}")
        fail(f"atomic publication failed for {final_name}: {os.strerror(error_number)}")


def rollback_publication(
    directory_fd: int,
    temporary_files: list[tuple[str, DirectoryIdentity]],
    published_names: list[tuple[str, DirectoryIdentity]],
    rename_candidates: list[tuple[str, DirectoryIdentity]],
) -> list[str]:
    errors: list[str] = []

    for final_name, published_identity in reversed(published_names):
        try:
            current = os.stat(final_name, dir_fd=directory_fd, follow_symlinks=False)
            if identity_of(current) != published_identity:
                errors.append(f"refused to remove replaced final {final_name}")
                continue
            os.unlink(final_name, dir_fd=directory_fd)
        except FileNotFoundError:
            pass
        except OSError as exc:
            errors.append(f"could not remove final {final_name}: {exc}")

    # A signal or explicit KeyboardInterrupt can arrive after renameat2 has
    # moved a file but before Python records the completed rename. Candidates
    # are registered before renameat2 so that uncertain successes are still
    # removed, but only when the final retains the temporary's exact identity.
    for final_name, candidate_identity in reversed(rename_candidates):
        try:
            current = os.stat(final_name, dir_fd=directory_fd, follow_symlinks=False)
            if identity_of(current) == candidate_identity:
                os.unlink(final_name, dir_fd=directory_fd)
        except FileNotFoundError:
            pass
        except OSError as exc:
            errors.append(f"could not inspect or remove candidate final {final_name}: {exc}")

    for temporary_name, temporary_identity in temporary_files:
        try:
            current = os.stat(temporary_name, dir_fd=directory_fd, follow_symlinks=False)
            if identity_of(current) != temporary_identity:
                errors.append(f"refused to remove replaced temporary {temporary_name}")
                continue
            os.unlink(temporary_name, dir_fd=directory_fd)
        except FileNotFoundError:
            pass
        except OSError as exc:
            errors.append(f"could not remove temporary {temporary_name}: {exc}")

    try:
        os.fsync(directory_fd)
    except OSError as exc:
        errors.append(f"could not sync output directory after rollback: {exc}")
    return errors


def publish(
    output: pathlib.Path,
    pairs: list[tuple[pathlib.Path, str]],
    expected_identity: DirectoryIdentity,
) -> None:
    directory_fd, initial = open_output_directory(output, expected_identity)
    temporary_files: list[tuple[str, DirectoryIdentity]] = []
    published_names: list[tuple[str, DirectoryIdentity]] = []
    rename_candidates: list[tuple[str, DirectoryIdentity]] = []
    cancellation_in_progress = False
    previous_signal_handlers: list[tuple[int, object]] = []

    def cancel_publication(signal_number: int, _frame: object) -> None:
        nonlocal cancellation_in_progress
        if cancellation_in_progress:
            return
        cancellation_in_progress = True
        raise PublicationCancelled(signal_number)

    try:
        for signal_number in (signal.SIGINT, signal.SIGTERM):
            previous_handler = signal.getsignal(signal_number)
            signal.signal(signal_number, cancel_publication)
            previous_signal_handlers.append((signal_number, previous_handler))
    except BaseException:
        for signal_number, previous_handler in reversed(previous_signal_handlers):
            signal.signal(signal_number, previous_handler)
        os.close(directory_fd)
        raise

    try:
        revalidate_namespace(output, directory_fd, initial)
        for source, final_name in pairs:
            require_safe_name(final_name)
            destination_absent(directory_fd, final_name)
            temporary_name = f".ava-publish.{os.getpid()}.{secrets.token_hex(12)}.tmp"
            copy_to_temporary(directory_fd, source, temporary_name, temporary_files)

        revalidate_namespace(output, directory_fd, initial)
        for (_, final_name), (temporary_name, temporary_identity) in zip(
            pairs,
            temporary_files,
            strict=True,
        ):
            destination_absent(directory_fd, final_name)
            rename_candidates.append((final_name, temporary_identity))
            rename_no_replace(directory_fd, temporary_name, final_name)
            published_names.append((final_name, temporary_identity))
        os.fsync(directory_fd)
        revalidate_namespace(output, directory_fd, initial)
    except BaseException as exc:
        cancellation_in_progress = True
        rollback_errors = rollback_publication(
            directory_fd,
            temporary_files,
            published_names,
            rename_candidates,
        )
        if rollback_errors:
            details = "; ".join(rollback_errors)
            raise RuntimeError(f"{exc}; publication rollback was incomplete: {details}") from exc
        raise
    finally:
        try:
            for signal_number, previous_handler in reversed(previous_signal_handlers):
                signal.signal(signal_number, previous_handler)
        finally:
            os.close(directory_fd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", type=pathlib.Path)
    parser.add_argument("--snapshot-executable", nargs=2, metavar=("SOURCE", "DESTINATION"))
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--expected-directory-identity")
    parser.add_argument("--file", nargs=2, action="append", metavar=("SOURCE", "NAME"), default=[])
    args = parser.parse_args()

    try:
        if args.check is not None:
            if args.snapshot_executable or args.output or args.expected_directory_identity or args.file:
                parser.error("--check cannot be combined with snapshot/publication arguments")
            fd, initial = open_output_directory(args.check)
            try:
                revalidate_namespace(args.check, fd, initial)
                print(format_identity(identity_of(initial)))
            finally:
                os.close(fd)
            return 0

        if args.snapshot_executable is not None:
            if args.output or args.expected_directory_identity or args.file:
                parser.error("--snapshot-executable cannot be combined with publication arguments")
            source, destination = args.snapshot_executable
            snapshot_executable(pathlib.Path(source), pathlib.Path(destination))
            return 0

        if not args.output or not args.expected_directory_identity or not args.file:
            parser.error(
                "publication requires --output, --expected-directory-identity, "
                "and at least one --file SOURCE NAME"
            )
        publish(
            args.output,
            [(pathlib.Path(source), name) for source, name in args.file],
            parse_identity(args.expected_directory_identity),
        )
        return 0
    except PublicationCancelled as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 128 + exc.signal_number
    except KeyboardInterrupt:
        print("error: publication cancelled by KeyboardInterrupt", file=sys.stderr)
        return 130
    except (OSError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
