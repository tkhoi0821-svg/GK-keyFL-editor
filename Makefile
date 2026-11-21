CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-misleading-indentation -Wno-unused-function
# Ban day du (cua so SDL3):   make
gk-keyfl-editor: gk_editor.c
	$(CC) $(CFLAGS) -DGK_SDL3 -o $@ $< $(shell pkg-config --cflags --libs sdl3) -lm
# Ban khong cua so (kiem thu, chup anh, nhung media): make headless && make test
headless: gk_editor.c
	$(CC) $(CFLAGS) -o gk-keyfl-editor-headless $< -lm
test: headless
	./gk-keyfl-editor-headless --selftest
clean:
	rm -f gk-keyfl-editor gk-keyfl-editor-headless
.PHONY: headless test clean
