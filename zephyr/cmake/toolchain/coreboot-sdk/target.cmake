# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Coreboot SDK uses GCC
set(COMPILER gcc)
set(LINKER ld)
set(BINTOOLS gnu)

# Map Target Architecture to Coreboot SDK Architecture
set(TARGET_ARCH "${ARCH}")
if("${ARCH}" STREQUAL "riscv")
  set(TARGET_ARCH "riscv64")
endif()

# Mapping of Zephyr architecture -> ec-coreboot-sdk toolchain
set(CROSS_COMPILE_TARGET_arm      arm-eabi)
set(CROSS_COMPILE_TARGET_riscv64  riscv64-elf)
set(CROSS_COMPILE_TARGET_x86      i386-elf)

set(CROSS_COMPILE_TARGET        ${CROSS_COMPILE_TARGET_${TARGET_ARCH}})
set(CROSS_COMPILE_QUALIFIER     "")

if("${TARGET_ARCH}" STREQUAL "arm")
  set(CROSS_COMPILE_QUALIFIER     "thumb/")
  if(CONFIG_ARM64)
    set(CROSS_COMPILE_TARGET      aarch64-elf)
  endif()
elseif("${TARGET_ARCH}" STREQUAL "x86" AND CONFIG_X86_64)
  set(CROSS_COMPILE_TARGET      x86_64-elf)
elseif("${TARGET_ARCH}" STREQUAL "riscv64")
  # TODO (b/404325494): Add coreboot sdk multilib support
  set(CROSS_COMPILE_QUALIFIER     "rv32imac/ilp32/")
endif()

if(DEFINED COREBOOT_SDK_ROOT_${TARGET_ARCH})
  set(COREBOOT_SDK_ROOT "${COREBOOT_SDK_ROOT_${TARGET_ARCH}}")
  set(COREBOOT_SDK_ROOT_LIBSTDCXX "${COREBOOT_SDK_ROOT_libstdcxx_${TARGET_ARCH}}")
  set(COREBOOT_SDK_ROOT_PICOLIBC "${COREBOOT_SDK_ROOT_picolibc_${TARGET_ARCH}}")
else()
  set(COREBOOT_SDK_ROOT_LIBSTDCXX "${COREBOOT_SDK_ROOT}")
  set(COREBOOT_SDK_ROOT_PICOLIBC "${COREBOOT_SDK_ROOT}")
endif()

set(CC gcc)
set(C++ g++)
set(TOOLCHAIN_HOME "${COREBOOT_SDK_ROOT}/bin")
set(CROSS_COMPILE "${CROSS_COMPILE_TARGET}-")

set(CMAKE_AR         "${TOOLCHAIN_HOME}/${CROSS_COMPILE}ar")
set(CMAKE_NM         "${TOOLCHAIN_HOME}/${CROSS_COMPILE}nm")
set(CMAKE_OBJCOPY    "${TOOLCHAIN_HOME}/${CROSS_COMPILE}objcopy")
set(CMAKE_OBJDUMP    "${TOOLCHAIN_HOME}/${CROSS_COMPILE}objdump")
set(CMAKE_RANLIB     "${TOOLCHAIN_HOME}/${CROSS_COMPILE}ranlib")
set(CMAKE_READELF    "${TOOLCHAIN_HOME}/${CROSS_COMPILE}readelf")
set(CMAKE_GCOV       "${TOOLCHAIN_HOME}/${CROSS_COMPILE}gcov")

# Compiler version isn't set yet, infer it from the directory name
file(GLOB GCC_DIR LIST_DIRECTORIES true "${COREBOOT_SDK_ROOT}/lib/gcc/${CROSS_COMPILE_TARGET}/[0-9][0-9].[0-9].[0-9]")
get_filename_component(GCC_VERSION ${GCC_DIR} NAME)

if(CONFIG_LTO)
  # Using 'ar' or 'ranlib' alone produces 'plugin needed to handle lto object',
  # use 'gcc-* ' variants to specify appropriate plugin argument.
  # For more details see
  # https://embeddedartistry.com/blog/2020/04/13/prefer-gcc-ar-to-ar-in-your-buildsystems/
  set(CMAKE_AR         "${TOOLCHAIN_HOME}/${CROSS_COMPILE}gcc-ar")
  set(CMAKE_RANLIB     "${TOOLCHAIN_HOME}/${CROSS_COMPILE}gcc-ranlib")
endif()

  ##########################################################################################################
  # TODO(b/384559486) Fix this block
if(CONFIG_REQUIRES_FULL_LIBCPP)
  # Add system include paths
  set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include")
  set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include/${CROSS_COMPILE_TARGET}")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include/${CROSS_COMPILE_TARGET}")
  set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include/libsupc++")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include/libsupc++")

  set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include/std")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -isystem ${COREBOOT_SDK_ROOT_LIBSTDCXX}/${CROSS_COMPILE_TARGET}/include/std")
endif()

if(CONFIG_PICOLIBC AND NOT CONFIG_PICOLIBC_USE_MODULE)
  # Move picolibc.specs files to a location that they can be found since zephyr adds the spec definition
  configure_file(${COREBOOT_SDK_ROOT_PICOLIBC}/picolibc/coreboot-${CROSS_COMPILE_TARGET}/picolibc.specs ${APPLICATION_BINARY_DIR}/ COPYONLY)
  configure_file(${COREBOOT_SDK_ROOT_PICOLIBC}/picolibc/coreboot-${CROSS_COMPILE_TARGET}/picolibcpp.specs ${APPLICATION_BINARY_DIR}/ COPYONLY)

  # Add system include paths
  set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -isystem ${COREBOOT_SDK_ROOT_PICOLIBC}/picolibc/coreboot-${CROSS_COMPILE_TARGET}")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -isystem ${COREBOOT_SDK_ROOT_PICOLIBC}/picolibc/coreboot-${CROSS_COMPILE_TARGET}")

  set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -isystem ${COREBOOT_SDK_ROOT_PICOLIBC}/picolibc/coreboot-${CROSS_COMPILE_TARGET}/include")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -isystem ${COREBOOT_SDK_ROOT_PICOLIBC}/picolibc/coreboot-${CROSS_COMPILE_TARGET}/include")

  # Place orphaned sections and disable the warning for them
  set(CONFIG_LINKER_ORPHAN_SECTION_WARN n)
  set(LINKER_ORPHAN_SECTION_PLACE y)
  ##########################################################################################################

  # Add picolibc
  message(INFO "Setting TOOLCHAIN_HAS_PICOLIBC to support full build.")
  set(TOOLCHAIN_HAS_PICOLIBC ON CACHE BOOL "True if toolchain supports picolibc")

  # Add newlib
  message(INFO "Setting TOOLCHAIN_HAS_NEWLIB to support full build.")
  set(TOOLCHAIN_HAS_NEWLIB ON CACHE BOOL "True if toolchain supports newlib")
endif()

# On ARM, we don't use libgcc: It's built against a fixed target (e.g.
# used instruction set, ABI, ISA extensions) and doesn't adapt when
# compiler flags change any of these assumptions. Use our own mini-libgcc
# instead.
if("${ARCH}" STREQUAL "arm" AND NOT CONFIG_PICOLIBC)
  set(no_libgcc True)
endif()
