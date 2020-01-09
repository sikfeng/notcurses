% notcurses_input(3)
% nick black <nickblack@linux.com>
% v1.1.0

# NAME

notcurses_input - input via notcurses

# SYNOPSIS

#include <notcurses.h>**

```c
typedef struct ncinput {
  char32_t id;     // Unicode codepoint
  int y;           // Y cell coordinate of event, -1 for undefined
  int x;           // X cell coordinate of event, -1 for undefined
  bool alt;        // Was Alt held during the event?
  bool shift;      // Was Shift held during the event?
  bool ctrl;       // Was Ctrl held during the event?
} ncinput;
```

**bool nckey_mouse_p(char32_t r);**

**char32_t notcurses_getc(struct notcurses* n, const struct timespec* ts, sigset_t* sigmask, ncinput* ni);**

**char32_t notcurses_getc_nblock(struct notcurses* n, ncinput* ni);**

**char32_t notcurses_getc_blocking(struct notcurses* n, ncinput* ni);**

**int notcurses_mouse_enable(struct notcurses* n);**

**int notcurses_mouse_disable(struct notcurses* n);**

# DESCRIPTION

notcurses supports input from keyboards and mice, and any device that looks
like them. Mouse support requires a broker such as GPM, Wayland, or Xorg, and
must be explicitly enabled via **notcurses_mouse_enable(3)**. The full 32-bit
range of Unicode is supported (see **unicode(7)**), with synthesized events
mapped into the [Supplementary Private Use Area-B](https://unicode.org/charts/PDF/U1.1.00.pdf).
Unicode characters are returned directly as UCS-32, one codepoint at a time.

notcurses takes its keyboard input from **stdin**, which will be placed into
non-blocking mode for the duration of operation. The terminal is put into raw
mode (see **cfmakeraw(3)**), and thus keys are received without line-buffering.
notcurses maintains its own buffer of input characters, which it will attempt
to fill whenever it reads.

**notcurses_getc(3)** allows a **struct timespec** to be specified as a timeout.
If **ts** is **NULL**, **notcurses_getc(3)** will block until it reads input, or
is interrupted by a signal. If its values are zeroes, there will be no blocking.
Otherwise, **ts** specifies a minimum time to wait for input before giving up.
On timeout, 0 is returned. Signals in **sigmask** will be masked and blocked in
the same manner as a call to **ppoll(2)**. **sigmask** may be **NULL**. Event
details will be reported in **ni**, unless **ni** is NULL.

## Mice

For mouse events, the additional fields **y** and **x** are set. These fields
are not meaningful for keypress events. Mouse events can be distinguished using
the **nckey_mouse_p(3)** predicate. Once enabled, mouse button presses are
detected, as are mouse motions made while a button is held down. If no button
is depressed, mouse movement _does not result in events_. This is known as
"button-event tracking" mode in the nomenclature of [Xterm Control
Sequences](https://www.xfree86.org/current/ctlseqs.html).

## Synthesized keypresses

Many keys do not have a Unicode representation, let alone ASCII. Examples
include the modifier keys (Alt, Meta, etc.), the "function" keys, and the arrow
keys on the numeric keypad. The special keys available to the terminal are
defined in the **terminfo(5)** entry, which notcurses loads on startup. Upon
receiving an escape code matching a terminfo input capability, notcurses
synthesizes a special value. An escape sequence must arrive in its entirety to
notcurses; running out of input in the middle of an escape sequence will see it
rejected. Likewise, any error while handling an escape sequence will see the
lex aborted, and the sequence thus far played back as independent literal
keystrokes.

The full list of synthesized keys (there are well over one hundred) can be
found in **<notcurses.h>**. For more details, consult **terminfo(5)**.

## **NCKEY_RESIZE**

Unless the **SIGWINCH** handler has been inhibited (see **notcurses_init(3)**),
notcurses will automatically catch screen resizes, and synthesize an
**NCKEY_RESIZE** event. Upon receiving this event, the user may call
**notcurses_resize(3)** to force an immediate reflow, or just wait until the
next call to **notcurses_render(3)**, when notcurses will pick up the resize
itself. If the **SIGWINCH** handler is inhibited, **NCKEY_RESIZE** is never
generated.

# RETURN VALUES

On error, the **_getc** family of functions returns **(char32_t)-1**. The cause of the error may be determined
using **errno(3)**. Unless the error was a temporary one (especially e.g. **EINTR**),
**notcurses_getc(3)** probably cannot be usefully called forthwith. On a
timeout, 0 is returned. Otherwise, the UCS-32 value of a Unicode codepoint, or
a synthesized event, is returned.

**notcurses_mouse_enable(3)** returns 0 on success, and non-zero on failure, as
does **notcurses_mouse_disable(3)**.

# NOTES

Like any other notcurses function, it is an error to call **notcurses_getc(3)**
during or after a call to **notcurses_stop(3)**. If a thread is always sitting
on blocking input, it can be tricky to guarantee that this doesn't happen.

# BUGS

Failed escape sequences are not yet played back in their entirety; only an
ESC (ASCII 0x1b) will be seen by the application.

# SEE ALSO

**notcurses(3)**, **poll(2)**, **unicode(7)**, **cfmakeraw(3)**, **terminfo(5)**,
**ascii(7)**, **signal(7)**, **termios(3)**