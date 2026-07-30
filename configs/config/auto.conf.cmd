deps_config := \
	/home/cf/embedded/embedded-sdk/board/StarrySkyC2/driver.kconfig \
	/home/cf/embedded/embedded-sdk/board/StarrySkyC2/board.kconfig \
	/home/cf/embedded/embedded-sdk/tools/kconfig/Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(BoardExport)" "/home/cf/embedded/embedded-sdk/board/StarrySkyC2/board.kconfig"
include/config/auto.conf: FORCE
endif
ifneq "$(DriverExport)" "/home/cf/embedded/embedded-sdk/board/StarrySkyC2/driver.kconfig"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
