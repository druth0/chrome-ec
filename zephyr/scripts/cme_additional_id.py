# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=line-too-long
"Dict of components for CME that are identified by probing a different component"

import collections


# A namedtuple to store the information for each compatible
AdditionalInfo = collections.namedtuple(
    "AdditionalInfo",
    [
        "name",
        "ctype",
        "port",
        "command_1",
        "command_2",
    ],
)

# A Dictionary that stores component information that are identified by config information
# on a different component.
# currently only used for PDC + retimer
ADDITIONAL_DICTIONARY = {
    "realtek,rts54": [
        AdditionalInfo(
            "realtek,jhl8040",
            "mux",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG01" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730310000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,jhl9040",
            "mux",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG04" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730340000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,jhl9040",
            "mux",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG05" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730350000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "parade,ps8762",
            "mux",
            0,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG06" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730360000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "ite,it5205",
            "mux",
            1,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG06" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730360000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,jhl9040",
            "mux",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG08" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730380000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,tusb1044",
            "mux",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0F" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730460000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,tusb1044",
            "mux",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0O" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f47304F0000000000",
                "bytes": 38,
            },
        ),
    ],
}
