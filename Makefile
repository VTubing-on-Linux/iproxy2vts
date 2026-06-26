CC = gcc
PKGS = libimobiledevice-1.0 libusbmuxd-2.0 gtk+-3.0 ayatana-appindicator3-0.1
CFLAGS = -Wall -O2 $(shell pkg-config --cflags $(PKGS)) -pthread
LDFLAGS = $(shell pkg-config --libs $(PKGS)) -pthread

TARGET = iproxy2vts
SRCS = main.c config.c notify.c iphone.c network.c bridge.c service.c
OBJS = $(SRCS:.c=.o)
HDRS = config.h log.h notify.h iphone.h network.h bridge.h service.h
SERVICE = iproxy2vts.service

PREFIX = $(HOME)/.local
BINDIR = $(PREFIX)/bin
SYSTEMD_USER_DIR = $(HOME)/.config/systemd/user
MANDIR = $(PREFIX)/share/man/man1

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean clear:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	@echo "Installing iproxy2vts..."
	@mkdir -p $(BINDIR)
	@mkdir -p $(SYSTEMD_USER_DIR)
	@mkdir -p $(MANDIR)
	@cp $(TARGET) $(BINDIR)/$(TARGET)
	@chmod +x $(BINDIR)/$(TARGET)
	@cp $(SERVICE) $(SYSTEMD_USER_DIR)/$(SERVICE)
	@echo ""
	@echo "Installation complete!"
	@echo ""
	@echo "To enable and start the service:"
	@echo "  systemctl --user daemon-reload"
	@echo "  systemctl --user enable iproxy2vts"
	@echo "  systemctl --user start iproxy2vts"
	@echo ""
	@echo "To check status:"
	@echo "  systemctl --user status iproxy2vts"
	@echo ""
	@echo "To view logs:"
	@echo "  journalctl --user -u iproxy2vts -f"


uninstall:
	@echo "Uninstalling iproxy2vts..."
	@systemctl --user stop iproxy2vts 2>/dev/null || true
	@systemctl --user disable iproxy2vts 2>/dev/null || true
	@rm -f $(BINDIR)/$(TARGET)
	@rm -f $(SYSTEMD_USER_DIR)/$(SERVICE)
	@systemctl --user daemon-reload
	@echo "Uninstallation complete!"

.PHONY: all clean clear install uninstall
