# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for ocelot."""


def register_npcx9_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    inherited_from=None,
):
    """Register an npcx9 based variant of ocelot."""
    if inherited_from is None:
        inherited_from = ["ocelot"]

    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        inherited_from=inherited_from,
    )


def register_it8xxx2_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    inherited_from=None,
):
    """Register an it8xxx2 based variant of ocelot."""
    if inherited_from is None:
        inherited_from = ["ocelot"]

    register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002aw",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        inherited_from=inherited_from,
    )


def register_mec172x_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    inherited_from=None,
):
    """Register an microchip based variant of ocelot."""
    if inherited_from is None:
        inherited_from = ["ocelot"]

    register_mchp_project(
        project_name=project_name,
        zephyr_board="mec172x/mec172x_nsz/mec1727",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        modules=["cmsis", "cmsis_6", "ec"],
        inherited_from=inherited_from,
    )


def register_rtk59_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
):
    """Register a realtek based variant of ocelot."""
    register_rtk_project(
        project_name=project_name,
        zephyr_board="realtek/rts5912",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        modules=["cmsis", "cmsis_6", "ec"],
    )


# For use on RVP SKU1 and SKU2
register_npcx9_project(
    project_name="ocelotrvp-npcx",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
    ],
)

# For use on RVP SKU3
register_it8xxx2_project(
    project_name="ocelotrvp-ite",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
    ],
)

# For use on RVP SKU4
register_mec172x_project(
    project_name="ocelotrvp-mchp",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
    ],
)

register_ish_project(
    project_name="ocelotrvp-ish",
    zephyr_board="intel_ish_5_8_0",
    dts_overlays=[
        here / "ocelot-ish" / "ocelotrvp-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "ocelot-ish" / "prj.conf",
        here / "ocelot-ish" / "motionsense.conf",
    ],
)

# For realtek
register_rtk59_project(
    project_name="ojal",
)


# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-npcx", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-ite", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-mchp", addr=0x40318)
assert_rw_fwid_DO_NOT_EDIT(project_name="ojal", addr=0x80404)
