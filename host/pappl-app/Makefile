CFLAGS += $(shell pkg-config --cflags pappl)
LIBS   += $(shell pkg-config --libs pappl)
silentype-app: silentype-app.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)
clean:
	rm -f silentype-app
