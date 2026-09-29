# Тести на ПК для модулів без HAL. Запуск з цієї папки: make
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O1 -g \
           -I../App/Inc -I../Drivers/BSP/joystick -I../Drivers/BSP/buzzer
BUILD   := build

ifeq ($(OS),Windows_NT)
EXE := .exe
CFLAGS += -D__USE_MINGW_ANSI_STDIO=1
endif

TESTS := test_access_ctrl test_access_list test_joystick_math test_event_log test_timeutil

test_access_ctrl_SRC   := ../App/Src/access_ctrl.c ../App/Src/menu.c ../App/Src/access_list.c \
                          ../App/Src/event_log.c ../App/Src/crc32.c fake_storage.c
test_access_list_SRC   := ../App/Src/access_list.c ../App/Src/crc32.c fake_storage.c
test_joystick_math_SRC := ../Drivers/BSP/joystick/joystick_math.c
test_event_log_SRC     := ../App/Src/event_log.c
test_timeutil_SRC      := ../App/Src/timeutil.c

BINS := $(addprefix $(BUILD)/,$(addsuffix $(EXE),$(TESTS)))

.PHONY: all test clean
all: test

test: $(BINS)
	@fail=0; for t in $(BINS); do ./$$t || fail=1; done; exit $$fail

.SECONDEXPANSION:
$(BUILD)/%$(EXE): %.c test.h $$($$*_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $($*_SRC)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
