# Cross build for Windows XP (i686, NT 5.01) + native Linux build for tests.
CC32 ?= i686-w64-mingw32-gcc
CC64 ?= x86_64-w64-mingw32-gcc
WINDRES32 ?= i686-w64-mingw32-windres
WINDRES64 ?= x86_64-w64-mingw32-windres
CFLAGS_COMMON = -O2 -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-stringop-truncation -Wno-format-truncation -std=gnu99 -Isrc
WINDEFS = -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D__USE_MINGW_ANSI_STDIO=0 -DUNICODE -D_UNICODE
LDXP32 = -static -static-libgcc -Wl,--major-subsystem-version,5,--minor-subsystem-version,1,--major-os-version,5,--minor-os-version,1
LDXP64 = -static -static-libgcc -Wl,--major-subsystem-version,5,--minor-subsystem-version,2,--major-os-version,5,--minor-os-version,2
CORE = src/core.c src/xcrypto.c src/third_party/monocypher.c src/third_party/monocypher-ed25519.c
LIBS = -lws2_32 -liphlpapi -lshell32 -ladvapi32
GUILIBS = $(LIBS) -lcomctl32 -lcomdlg32 -lgdi32 -luser32

all: build/x86/LanMessenger.exe build/x86/lanmsg-cli.exe build/x64/LanMessenger.exe build/x64/lanmsg-cli.exe build/linux/lanmsg-cli

build/x86/res.o: src/res.rc src/app.manifest src/app.ico
	mkdir -p build/x86 && $(WINDRES32) -Isrc src/res.rc -O coff -o $@
build/x64/res.o: src/res.rc src/app.manifest src/app.ico
	mkdir -p build/x64 && $(WINDRES64) -Isrc src/res.rc -O coff -o $@
build/x86/LanMessenger.exe: $(CORE) src/gui.c src/core.h build/x86/res.o
	$(CC32) $(CFLAGS_COMMON) $(WINDEFS) -mwindows -municode -o $@ src/gui.c $(CORE) build/x86/res.o $(LDXP32) $(GUILIBS)
build/x86/lanmsg-cli.exe: $(CORE) src/cli.c src/core.h
	mkdir -p build/x86 && $(CC32) $(CFLAGS_COMMON) $(WINDEFS) -mconsole -o $@ src/cli.c $(CORE) $(LDXP32) $(LIBS)
build/x64/LanMessenger.exe: $(CORE) src/gui.c src/core.h build/x64/res.o
	$(CC64) $(CFLAGS_COMMON) $(WINDEFS) -mwindows -municode -o $@ src/gui.c $(CORE) build/x64/res.o $(LDXP64) $(GUILIBS)
build/x64/lanmsg-cli.exe: $(CORE) src/cli.c src/core.h
	mkdir -p build/x64 && $(CC64) $(CFLAGS_COMMON) $(WINDEFS) -mconsole -o $@ src/cli.c $(CORE) $(LDXP64) $(LIBS)
build/linux/lanmsg-cli: $(CORE) src/cli.c src/core.h
	mkdir -p build/linux && cc $(CFLAGS_COMMON) -g -o $@ src/cli.c $(CORE) -lpthread

build/linux/lan-messenger-gtk: $(CORE) linux/gtk_gui.c src/core.h
	mkdir -p build/linux && cc $(CFLAGS_COMMON) -Wno-deprecated-declarations -o $@ linux/gtk_gui.c $(CORE) $$(pkg-config --cflags --libs gtk+-3.0) -lpthread -ldl
linux: build/linux/lanmsg-cli build/linux/lan-messenger-gtk

clean:
	rm -rf build dist
.PHONY: all clean linux

# relay.c and resume.c are #included by core.c
build/x86/LanMessenger.exe build/x86/lanmsg-cli.exe build/x64/LanMessenger.exe build/x64/lanmsg-cli.exe build/linux/lanmsg-cli: src/relay.c src/resume.c
