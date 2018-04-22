
default: all

.DEFAULT:
	cd deps && $(MAKE) $@
	cd src && $(MAKE) $@
