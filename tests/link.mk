# Link play (docs/link-play.md): the session protocol over the loopback transport.
build/test_link_protocol: tests/test_link_protocol.c src/link_protocol.c include/colosseum3ds/link_protocol.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_link_protocol.c src/link_protocol.c $(LDFLAGS) -o $@
all: build/test_link_protocol
.PHONY: link-check
link-check: build/test_link_protocol
	./build/test_link_protocol
test: link-check
