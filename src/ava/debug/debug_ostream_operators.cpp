#include "sys.h"
#include "debug_ostream_operators.h"
#include <termios.h>
#include <libcwd/buf2str.h>

namespace {

void append_field(std::string& in_out, char const* name)
{
  if (in_out.empty())
  {
    in_out = name;
    return;
  }

  in_out.append("|");
  in_out.append(name);
}

std::string termios_ciflags_to_string(tcflag_t iflags)
{
  std::string result;

  #define ADD_BIT_FIELD(field) if ((iflags & field)) append_field(result, #field)
  ADD_BIT_FIELD(IGNBRK);
  ADD_BIT_FIELD(BRKINT);
  ADD_BIT_FIELD(IGNPAR);
  ADD_BIT_FIELD(PARMRK);
  ADD_BIT_FIELD(INPCK);
  ADD_BIT_FIELD(ISTRIP);
  ADD_BIT_FIELD(INLCR);
  ADD_BIT_FIELD(IGNCR);
  ADD_BIT_FIELD(ICRNL);
  ADD_BIT_FIELD(IUCLC);
  ADD_BIT_FIELD(IXON);
  ADD_BIT_FIELD(IXANY);
  ADD_BIT_FIELD(IXOFF);
  ADD_BIT_FIELD(IMAXBEL);
  ADD_BIT_FIELD(IUTF8);
  #undef ADD_BIT_FIELD

  return result;
}

std::string termios_coflags_to_string(tcflag_t oflags)
{
  std::string result;

  #define ADD_BIT_FIELD(field) if ((oflags & field)) append_field(result, #field)
  ADD_BIT_FIELD(OPOST);
  ADD_BIT_FIELD(OLCUC);
  ADD_BIT_FIELD(ONLCR);
  ADD_BIT_FIELD(OCRNL);
  ADD_BIT_FIELD(ONOCR);
  ADD_BIT_FIELD(ONLRET);
  ADD_BIT_FIELD(OFILL);
  ADD_BIT_FIELD(OFDEL);
#ifdef __USE_MISC
  ADD_BIT_FIELD(XTABS);
#endif
  #undef ADD_BIT_FIELD

  append_field(result, "NLDLY=");
  switch ((oflags & NLDLY))
  {
    case NL0:
      result += "NL0";
      break;
    case NL1:
      result += "NL1";
      break;
    default:
      result += "?";
      break;
  }

  append_field(result, "CRDLY=");
  switch ((oflags & CRDLY))
  {
    case CR0:
      result += "CR0";
      break;
    case CR1:
      result += "CR1";
      break;
    case CR2:
      result += "CR2";
      break;
    case CR3:
      result += "CR3";
      break;
    default:
      result += "?";
      break;
  }

  append_field(result, "TABDLY=");
  switch ((oflags & TABDLY))
  {
    case TAB0:
      result += "TAB0";
      break;
    case TAB1:
      result += "TAB1";
      break;
    case TAB2:
      result += "TAB2";
      break;
    case TAB3:
      result += "TAB3";
      break;
    default:
      result += "?";
      break;
  }

  append_field(result, "BSDLY=");
  switch ((oflags & BSDLY))
  {
    case BS0:
      result += "BS0";
      break;
    case BS1:
      result += "BS1";
      break;
    default:
      result += "?";
      break;
  }

  append_field(result, "FFDLY=");
  switch ((oflags & FFDLY))
  {
    case FF0:
      result += "FF0";
      break;
    case FF1:
      result += "FF1";
      break;
    default:
      result += "?";
      break;
  }

  append_field(result, "VTDLY=");
  switch ((oflags & VTDLY))
  {
    case VT0:
      result += "VT0";
      break;
    case VT1:
      result += "VT1";
      break;
    default:
      result += "?";
      break;
  }

  return result;
}

std::string termios_ccflags_to_string(tcflag_t cflags)
{
  std::string result;

  append_field(result, "CSIZE=");
  switch ((cflags & CSIZE))
  {
    case CS5:
      result += "CS5";
      break;
    case CS6:
      result += "CS6";
      break;
    case CS7:
      result += "CS7";
      break;
    case CS8:
      result += "CS8";
      break;
    default:
      result += "?";
      break;
  }

  #define ADD_BIT_FIELD(field) if ((cflags & field)) append_field(result, #field)
  ADD_BIT_FIELD(CSTOPB);
  ADD_BIT_FIELD(CREAD);
  ADD_BIT_FIELD(PARENB);
  ADD_BIT_FIELD(PARODD);
  ADD_BIT_FIELD(HUPCL);
  ADD_BIT_FIELD(CLOCAL);
#ifdef __USE_MISC
  ADD_BIT_FIELD(ADDRB);
  ADD_BIT_FIELD(CMSPAR);
  ADD_BIT_FIELD(CRTSCTS);
#endif
  #undef ADD_BIT_FIELD

  if ((cflags & CBAUD) != 0)
    append_field(result, ("CBAUD=" + std::to_string((cflags & CBAUD))).c_str());

  if ((cflags & CIBAUD) != 0)
    append_field(result, ("CIBAUD=" + std::to_string((cflags & CIBAUD))).c_str());

  return result;
}

std::string termios_clflags_to_string(tcflag_t lflags)
{
  std::string result;

  #define ADD_BIT_FIELD(field) if ((lflags & field)) append_field(result, #field)
  ADD_BIT_FIELD(ISIG);
  ADD_BIT_FIELD(ICANON);
#if defined __USE_MISC || (defined __USE_XOPEN && !defined __USE_XOPEN2K)
  ADD_BIT_FIELD(XCASE);
#endif
  ADD_BIT_FIELD(ECHO);
  ADD_BIT_FIELD(ECHOE);
  ADD_BIT_FIELD(ECHOK);
  ADD_BIT_FIELD(ECHONL);
  ADD_BIT_FIELD(NOFLSH);
  ADD_BIT_FIELD(TOSTOP);
#ifdef __USE_MISC
  ADD_BIT_FIELD(ECHOCTL);
  ADD_BIT_FIELD(ECHOPRT);
  ADD_BIT_FIELD(ECHOKE);
  ADD_BIT_FIELD(FLUSHO);
  ADD_BIT_FIELD(PENDIN);
#endif
  ADD_BIT_FIELD(IEXTEN);
#ifdef __USE_MISC
  ADD_BIT_FIELD(EXTPROC);
#endif
  #undef ADD_BIT_FIELD

  return result;
}

} // namespace

namespace debug::ostream_operators {

std::ostream& operator<<(std::ostream& os, struct termios const& te)
{
  return os << "{c_iflag:" << termios_ciflags_to_string(te.c_iflag) <<
              ", c_oflag:" << termios_coflags_to_string(te.c_oflag) <<
              ", c_cflag:" << termios_ccflags_to_string(te.c_cflag) <<
              ", c_lflag:" << termios_clflags_to_string(te.c_lflag) <<
              ", c_cc:" << libcwd::buf2str(reinterpret_cast<char const*>(te.c_cc), sizeof(te.c_cc)) << '}';
}

//=============================================================================

} // namespace debug::ostream_operators
