QMAKE = qmake
QMAKE_PROJECT = BubbleSimulator.pro
QMAKE_MAKEFILE = Makefile.qt

.DEFAULT_GOAL := all

.PHONY: all clean distclean rebuild run qmake

all: $(QMAKE_MAKEFILE)
	$(MAKE) -f $(QMAKE_MAKEFILE) all

$(QMAKE_MAKEFILE): $(QMAKE_PROJECT)
	$(QMAKE) -o $(QMAKE_MAKEFILE) $(QMAKE_PROJECT)

clean: $(QMAKE_MAKEFILE)
	$(MAKE) -f $(QMAKE_MAKEFILE) clean

distclean: clean
	rm -f $(QMAKE_MAKEFILE) .qmake.stash BubbleSimulator

rebuild: distclean all

run: all
	./BubbleSimulator

qmake: $(QMAKE_MAKEFILE)
