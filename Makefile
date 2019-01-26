CFLAGS:=-g -O2 -fstack-protector-strong -Wformat -Werror=format-security -Wall
LDFLAGS:=-Wl,-Bsymbolic-functions -Wl,-z,relro

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

all: dell-fan-mon

dell-fan-mon: dell-fan-mon.c dell-fan-mon.h

clean:
	rm -f dell-fan-mon *.o

install:
	service dell-fan-mon stop
	install ./dell-fan-mon /usr/bin/
#	mv /etc/dell-fan-mon.conf /etc/dell-fan-mon.conf.prev
#	cp ./dell-fan-mon.conf /etc/
	service dell-fan-mon start
