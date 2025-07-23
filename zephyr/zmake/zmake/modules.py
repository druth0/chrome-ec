# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Registry of known Zephyr modules."""

import os

from zmake import build_config
from zmake import util


def third_party_module(name, checkout):
    """Common callback in registry for all third_party/zephyr modules.

    Args:
        name: The name of the module.
        checkout: The path to the chromiumos source.

    Return:
        The path to the module module.
    """
    return checkout / "src" / "third_party" / "zephyr" / name


def chre_module(name, checkout):
    """Special handler for CHRE to allow internal testing

    By default this function will resolve to the public CHRE repo, but if the
    environment variable PLATFORM_EC_USE_INTERNAL_CHRE is set, then the internal
    manifest's checkout of CHRE is returned.

    Args:
        name: The name of the module.
        checkout: The path to the chromiumos source.

    Return:
        The path to the module module.
    """
    if "PLATFORM_EC_USE_INTERNAL_CHRE" in os.environ:
        name += "_internal"
    return (
        checkout
        / "src"
        / "third_party"
        / "android"
        / "platform"
        / "system"
        / name
    )


known_modules = {
    # TODO(b/384581513): boringssl is not officially recognized by Zephyr,
    # since it doesn't have a zephyr/module.yaml. That doesn't prevent us from
    # using it with zmake.
    "boringssl": lambda name, checkout: (
        checkout / "src" / "third_party" / name
    ),
    "hal_stm32": third_party_module,
    "chre": chre_module,
    "cmsis": third_party_module,
    "cmsis_6": third_party_module,
    "ec": lambda name, checkout: (checkout / "src" / "platform" / "ec"),
    "egis": lambda name, checkout: (
        checkout / "src" / "platform" / "fingerprint" / "egis"
    ),
    "elan": lambda name, checkout: (
        checkout / "src" / "platform" / "fingerprint" / "elan"
    ),
    "fpc": lambda name, checkout: (
        checkout / "src" / "platform" / "fingerprint" / "fpc"
    ),
    "nanopb": third_party_module,
    "pigweed": lambda name, checkout: (checkout / "src" / "third_party" / name),
    "hal_intel_public": third_party_module,
    "picolibc": third_party_module,
    "intel_module_private": third_party_module,
}


def locate_from_checkout(checkout_dir):
    """Find modules from a Chrome OS checkout.

    Important: this function should only conditionally be called if a
    checkout exists.  Zmake *can* be used without a Chrome OS source
    tree.  You should call locate_from_directory if outside of a
    Chrome OS source tree.

    Args:
        checkout_dir: The path to the chromiumos source.

    Returns:
        A dictionary mapping module names to paths.
    """
    result = {}
    for name, locator in known_modules.items():
        path = locator(name, checkout_dir)
        if path.exists():
            result[name] = path
    return result


def locate_from_directory(directory):
    """Create a modules dictionary from a directory.

    This takes a directory, and searches for the known module names
    located in it.

    Args:
        directory: the directory to search in.

    Returns:
        A dictionary mapping module names to paths.
    """
    result = {}

    for name in known_modules:
        modpath = (directory / name).resolve()
        if (modpath / "zephyr" / "module.yml").is_file():
            result[name] = modpath

    return result


def setup_module_symlinks(output_dir, modules):
    """Setup a directory with symlinks to modules.

    Args:
        output_dir: The directory to place the symlinks in.
        modules: A dictionary of module names mapping to paths.

    Returns:
        The resultant BuildConfig that should be applied to use each
        of these modules.
    """
    if not output_dir.exists():
        output_dir.mkdir(parents=True)

    module_links = []

    for name, path in modules.items():
        link_path = output_dir.resolve() / name
        util.update_symlink(path, link_path)
        module_links.append(link_path)

    if module_links:
        return build_config.BuildConfig(
            cmake_defs={"ZEPHYR_MODULES": ";".join(map(str, module_links))}
        )
    return build_config.BuildConfig()


def default_projects_dirs(modules):
    """Get the default search path for projects.

    Args:
        modules: A dictionary of module names mapping to paths.

    Returns:
        A list of paths that should be searched for projects, if they exist.
    """
    ret = []
    if "ec" in modules:
        ret.append(modules["ec"] / "zephyr" / "program")
    return ret
