# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
# -*- coding: utf-8 -*-
#
# Common (non-language-specific) configuration for Sphinx
#

# type: ignore
# pylint: disable=wildcard-import
# pylint: disable=undefined-variable
# ruff: noqa: F405

from __future__ import print_function, unicode_literals

import pathlib
import re
import subprocess

from esp_docs.conf_docs import *  # noqa: F403,F401

# IDF_PATH validation removed - not needed for standalone component docs
# Only required when using ESP-IDF extensions that depend on IDF environment


extensions += ["sphinx_copybutton",  # noqa: F405
               # Needed as a trigger for running doxygen
               "esp_docs.esp_extensions.dummy_build_system",
               "esp_docs.esp_extensions.run_doxygen"
               ]

# link roles config
github_repo = "espressif/esp-mqtt"

# context used by sphinx_idf_theme
html_context["github_user"] = "espressif"  # noqa: F405
html_context["github_repo"] = "esp-mqtt"  # noqa: F405

# Extra options required by sphinx_idf_theme
project_slug = "esp-mqtt"
versions_url = "./_static/mqtt_docs_versions.js"
html_static_path = ["../_static"]

DOCS_MIN_VERSION = (1, 0, 0)
DOCS_MAX_ACTIVE_MINORS = 3
DOCS_EOL_MINORS = set()


def _format_documentation_versions(tags):
    parsed = []
    for tag in tags:
        match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", tag)
        if not match:
            continue
        ver = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
        if ver < DOCS_MIN_VERSION:
            continue
        parsed.append((ver, tag))

    parsed.sort(key=lambda item: item[0], reverse=True)

    minor_groups = []
    minor_map = {}
    for ver, tag in parsed:
        minor_key = (ver[0], ver[1])
        if minor_key not in minor_map:
            minor_map[minor_key] = []
            minor_groups.append(minor_key)
        minor_map[minor_key].append(tag)

    target_suffix = 'has_targets: true, supported_targets: ["esp32"]'
    lines = ['        { name: "latest", %s }' % target_suffix]
    for minor_idx, minor_key in enumerate(minor_groups):
        is_eol = minor_key in DOCS_EOL_MINORS or (
            DOCS_MAX_ACTIVE_MINORS is not None and minor_idx >= DOCS_MAX_ACTIVE_MINORS
        )
        for patch_idx, tag in enumerate(minor_map[minor_key]):
            if is_eol:
                flags = "old: true, end_of_life: true"
            elif patch_idx == 0:
                flags = "old: false"
            else:
                flags = "old: true"
            lines.append('        { name: "%s", %s, %s }' % (tag, flags, target_suffix))

    body = ",\n".join(lines)
    return (
        "var DOCUMENTATION_VERSIONS = {\n"
        '    DEFAULTS: { has_targets: true, supported_targets: ["esp32"] },\n'
        "    VERSIONS: [\n"
        "%s\n"
        "    ],\n"
        "    IDF_TARGETS: [\n"
        '        { text: "ESP32", value: "esp32" }\n'
        "    ]\n"
        "};\n"
        "\n"
        "(function () {\n"
        '    if (typeof window === "undefined" || typeof DOCUMENTATION_OPTIONS === "undefined") {\n'
        "        return;\n"
        "    }\n"
        '    var key = "esp_mqtt_allow_latest";\n'
        '    var ref = document.referrer || "";\n'
        '    var fromDocs = ref.indexOf("/projects/esp-mqtt/") !== -1;\n'
        '    if (DOCUMENTATION_OPTIONS.RELEASE === "latest") {\n'
        '        if (fromDocs || sessionStorage.getItem(key) === "1") {\n'
        '            sessionStorage.setItem(key, "1");\n'
        "        } else {\n"
        '            window.location.replace(window.location.href.replace("/latest/", "/stable/"));\n'
        "        }\n"
        "    } else {\n"
        "        sessionStorage.removeItem(key);\n"
        "    }\n"
        "})();\n"
    ) % body


def _write_documentation_versions():
    docs_dir = pathlib.Path(__file__).resolve().parent
    try:
        tags = subprocess.check_output(
            ["git", "tag", "-l"],
            cwd=docs_dir.parent,
            text=True,
            stderr=subprocess.DEVNULL,
        ).splitlines()
    except (OSError, subprocess.CalledProcessError):
        tags = []
    path = docs_dir / "_static" / "mqtt_docs_versions.js"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(_format_documentation_versions(tags))


_write_documentation_versions()

idf_targets = [ "esp32" ]
languages = ["en", "zh_CN"]
