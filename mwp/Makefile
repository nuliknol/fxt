TRAINER_DIR := trainer
GUI_DIR := gui
GUI_BUILD_DIR := $(GUI_DIR)/build
GUI_CACHE := $(GUI_BUILD_DIR)/CMakeCache.txt

.PHONY: all trainer gui gui-configure clean trainer-clean gui-clean rebuild test

all: trainer gui

trainer:
	$(MAKE) -C $(TRAINER_DIR)

gui-configure:
	@if [ -f "$(GUI_CACHE)" ] && ! grep -Fq "CMAKE_HOME_DIRECTORY:INTERNAL=$$(cd $(GUI_DIR) && pwd)" "$(GUI_CACHE)"; then \
		rm -rf "$(GUI_BUILD_DIR)"; \
	fi
	cmake -S $(GUI_DIR) -B $(GUI_BUILD_DIR)

gui: gui-configure
	cmake --build $(GUI_BUILD_DIR) -j

test: trainer
	$(MAKE) -C $(TRAINER_DIR) test

trainer-clean:
	$(MAKE) -C $(TRAINER_DIR) clean

gui-clean:
	rm -rf $(GUI_BUILD_DIR)

clean: trainer-clean gui-clean

rebuild: clean all
