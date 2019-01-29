CFLAGS:=-g -O2 -fstack-protector-strong -Wformat -Werror=format-security -Wall
LDFLAGS:=-Wl,-Bsymbolic-functions -Wl,-z,relro
INITDCTL:=$(shell which chkconfig)
SYSTEMCTL:=$(shell which systemctl)
SERVICE:=$(shell which service)
BIN_DIR=/usr/bin
CFG_DIR=/etc
SYSTEMD_DIR = /etc/systemd/system
INITD_DIR = /etc/init.d
NAME=dell-fan-mon
export DEB_BUILD_MAINT_OPTIONS = hardening=+all

all: dell-fan-mon

dell-fan-mon: dell-fan-mon.c dell-fan-mon.h

clean:
	rm -f $(NAME) *.o
test:
	$(NAME) --test
	
install:
	if test -f "$(SYSTEMD_DIR)/$(NAME).service" || test -f "$(INITD_DIR)/$(NAME)"; then $(SERVICE) $(NAME) stop; fi
	install -m0755 $(NAME) $(BIN_DIR)
	if test ! -f "$(CFG_DIR)/$(NAME).conf"; then install -m0644 $(NAME).conf $(CFG_DIR); fi 
	if test -x "$(SYSTEMCTL)" && test -d "$(SYSTEMD_DIR)"; then install -m0644 $(NAME).service $(SYSTEMD_DIR)/$(NAME).service && $(SYSTEMCTL) daemon-reload && $(SYSTEMCTL) enable $(NAME).service; fi
	if test -x "$(INITDCTL)" && test -d "$(INITD_DIR)"; then install -m0755 $(NAME).init $(INITD_DIR)/$(NAME) && $(INITDCTL) --add $(NAME) && $(INITDCTL) $(NAME) on; fi
	$(SERVICE) $(NAME) start
