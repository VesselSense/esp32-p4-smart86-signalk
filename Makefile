PORT ?= /dev/cu.usbserial-*

GET_IDF := source $(HOME)/esp/esp-idf/export.sh

.PHONY: build flash flash-monitor monitor menuconfig clean

build:
	bash -c 'export PATH="/opt/homebrew/bin:$$PATH" && $(GET_IDF) > /dev/null 2>&1 && idf.py build'

flash:
	bash -c 'export PATH="/opt/homebrew/bin:$$PATH" && $(GET_IDF) > /dev/null 2>&1 && idf.py -p $(PORT) flash'

flash-monitor:
	bash -c 'export PATH="/opt/homebrew/bin:$$PATH" && $(GET_IDF) > /dev/null 2>&1 && idf.py -p $(PORT) flash monitor'

monitor:
	bash -c 'export PATH="/opt/homebrew/bin:$$PATH" && $(GET_IDF) > /dev/null 2>&1 && idf.py -p $(PORT) monitor'

menuconfig:
	bash -c 'export PATH="/opt/homebrew/bin:$$PATH" && $(GET_IDF) > /dev/null 2>&1 && idf.py menuconfig'

clean:
	bash -c 'export PATH="/opt/homebrew/bin:$$PATH" && $(GET_IDF) > /dev/null 2>&1 && idf.py fullclean'

# Capture serial output to /tmp/serial_capture.txt (non-interactive, 20s)
capture:
	python3 scripts/capture_serial.py $(PORT) 20
