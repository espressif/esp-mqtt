# -*- coding: utf-8 -*-
#
# Common (non-language-specific) configuration for Sphinx
#

# type: ignore
# pylint: disable=wildcard-import
# pylint: disable=undefined-variable

from __future__ import print_function, unicode_literals

from esp_docs.conf_docs import *  # noqa: F403,F401

# IDF_PATH validation removed - not needed for standalone component docs
# Only required when using ESP-IDF extensions that depend on IDF environment


extensions += ['sphinx_copybutton',
               # Needed as a trigger for running doxygen
               'esp_docs.esp_extensions.dummy_build_system',
               'esp_docs.esp_extensions.run_doxygen'
               ]

# link roles config
github_repo = 'espressif/esp-mqtt'

# context used by sphinx_idf_theme
html_context['github_user'] = 'espressif'
html_context['github_repo'] = 'esp-mqtt'

# Extra options required by sphinx_idf_theme
project_slug = 'esp-mqtt'
versions_url = './_static/mqtt_docs_versions.js'

idf_targets = [ 'esp32' ]
languages = ['en']



