CC=cc
CFLAGS=-g -O2 -Wformat -Werror=format-security -Wall
#LDFLAGS:=-Wl,-Bsymbolic-functions -Wl,-z,relro
INITDCTL=$(shell which chkconfig)
SYSTEMCTL=$(shell which systemctl)
SERVICE=$(shell which service)
BIN_DIR=/usr/bin
MAN_DIR=/usr/man/man1
CFG_DIR=/etc
SYSTEMD_DIR=/etc/systemd/system
INITD_DIR=/etc/init.d
NAME=dell-fan-mon

all: dell-fan-mon man-page
	@echo "Finish"
dell-fan-mon: dell-fan-mon.c dell-fan-mon.h
	$(CC) $(CFLAGS) $(NAME).c -o $(NAME)
man-page:
	gzip -c $(NAME).1 > $(NAME).1.gz
clean:
	rm -f $(NAME) *.o $(NAME).1.gz
test:
	./$(NAME) --test
	
install:
	if test -f "$(SYSTEMD_DIR)/$(NAME).service" || test -f "$(INITD_DIR)/$(NAME)"; then $(SERVICE) $(NAME) stop; fi
	install -m0755 $(NAME) $(BIN_DIR)
	@mkdir -p $(MAN_DIR)
	install -m0644 $(NAME).1.gz $(MAN_DIR)
	if test ! -f "$(CFG_DIR)/$(NAME).conf"; then install -m0644 $(NAME).conf $(CFG_DIR); fi 
	if test -x "$(SYSTEMCTL)" && test -d "$(SYSTEMD_DIR)"; then install -m0644 $(NAME).service $(SYSTEMD_DIR)/$(NAME).service && $(SYSTEMCTL) daemon-reload && $(SYSTEMCTL) enable $(NAME).service; fi
	if test -x "$(INITDCTL)" && test -d "$(INITD_DIR)"; then install -m0755 $(NAME).init $(INITD_DIR)/$(NAME) && $(INITDCTL) --add $(NAME); fi
	$(SERVICE) $(NAME) start
	

uninstall:
	$(SERVICE) $(NAME) stop
	rm -f $(BIN_DIR)/$(NAME)
	rm -f $(MAN_DIR)/$(NAME).1.gz
	if test -x "$(SYSTEMCTL)" && test -f "$(SYSTEMD_DIR)/$(NAME).service"; then $(SYSTEMCTL) disable $(NAME).service && rm -f $(SYSTEMD_DIR)/$(NAME).service && $(SYSTEMCTL) daemon-reload ; fi
	if test -x "$(INITDCTL)" && test -f "$(INITD_DIR)/$(NAME)"; then $(INITDCTL) --del $(NAME) && rm -f $(INITD_DIR)/$(NAME); fi
