# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for fatcat."""


def register_npcx9_project(
    project_name,
    extra_kconfig_files=(),
    inherited_from=None,
):
    """Register an npcx9 based variant of fatcat."""
    if inherited_from is None:
        inherited_from = ["fatcat"]

    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
        inherited_from=inherited_from,
    )


def register_it8xxx2_project(
    project_name,
    extra_kconfig_files=(),
    extra_modules=(),
    inherited_from=None,
):
    """Register an it8xxx2 based variant of fatcat."""
    if inherited_from is None:
        inherited_from = ["fatcat"]

    register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002aw",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
        modules=["ec", *extra_modules],
        inherited_from=inherited_from,
    )


def register_realtek_project(
    project_name,
    extra_kconfig_files=(),
):
    """Register an realtek_ec based variant of fatcat."""
    register_rtk_project(
        project_name=project_name,
        zephyr_board="realtek/rts5912",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
    )


register_npcx9_project(
    project_name="fatcatrvp-npcx",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
        here / ".." / "intelrvp" / "ptlrvp" / "pd.conf",
        here / "rvp_program.conf",
    ],
)

register_it8xxx2_project(
    project_name="fatcatrvp-ite",
    extra_kconfig_files=[
        here / ".." / "intelrvp" / "zephyr_ap_pwrseq.conf",
        here / ".." / "intelrvp" / "ptlrvp" / "pd.conf",
        here / "rvp_program.conf",
    ],
)

register_npcx9_project(
    project_name="francka",
)

register_it8xxx2_project(
    project_name="felino",
)

register_it8xxx2_project(
    project_name="felino4es",
    extra_kconfig_files=[
        # Parent project's config
        here / "felino" / "project.conf",
        # Project-specific KConfig customization.
        here / "felino4es" / "project.conf",
    ],
)

register_it8xxx2_project(
    project_name="kinmen",
    extra_kconfig_files=[
        here / "dsp_comms.conf",
    ],
    extra_modules=["pigweed", "nanopb"],
)

register_realtek_project(
    project_name="lapis",
    extra_kconfig_files=[],
)

register_ish_project(
    project_name="kinmen-ish",
    zephyr_board="intel_ish_5_8_0",
    dts_overlays=[
        here / "kinmen-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "dsp_comms.conf",
        here / "kinmen-ish" / "project.conf",
    ],
    modules=["ec", "cmsis", "cmsis_6", "hal_intel_public", "pigweed", "nanopb"],
    inherited_from=["fatcat"],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="fatcatrvp-npcx", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="fatcatrvp-ite", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="francka", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="felino", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="felino4es", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="kinmen", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="lapis", addr=0x80404)
