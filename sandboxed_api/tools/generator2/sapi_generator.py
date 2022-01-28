# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""SAPI interface header generator.

Parses headers to extract type information from functions and generate a SAPI
interface wrapper.
"""
import sys

from absl import app
from absl import flags
from absl import logging
import code

FLAGS = flags.FLAGS

flags.DEFINE_string('sapi_name', None, 'library name')
flags.DEFINE_string('sapi_out', '', 'output header file')
flags.DEFINE_string('sapi_ns', '', 'namespace')
flags.DEFINE_string('sapi_isystem', '', 'system includes')
flags.DEFINE_list('sapi_functions', [], 'function list to analyze')
flags.DEFINE_list('sapi_in', None, 'input files to analyze')
flags.DEFINE_string('sapi_embed_dir', '', 'directory with embed includes')
flags.DEFINE_string('sapi_embed_name', '', 'name of the embed object')
flags.DEFINE_bool(
    'sapi_limit_scan_depth', False,
    'scan only functions from top level file in compilation unit')


def extract_includes(path, array):
  try:
    with open(path, 'r') as f:
      for line in f:
        array.append('-isystem')
        array.append(line.strip())
  except IOError:
    pass
  return array


def main(c_flags):
  # remove path to current binary
  c_flags.pop(0)
  logging.debug(FLAGS.sapi_functions)
  extract_includes(FLAGS.sapi_isystem, c_flags)
  tus = code.Analyzer.process_files(FLAGS.sapi_in, c_flags,
                                    FLAGS.sapi_limit_scan_depth)
  generator = code.Generator(tus)
  result = generator.generate(FLAGS.sapi_name, FLAGS.sapi_functions,
                              FLAGS.sapi_ns, FLAGS.sapi_out,
                              FLAGS.sapi_embed_dir, FLAGS.sapi_embed_name)

  if FLAGS.sapi_out:
    with open(FLAGS.sapi_out, 'w') as out_file:
      out_file.write(result)
  else:
    sys.stdout.write(result)


if __name__ == '__main__':
  flags.mark_flags_as_required(['sapi_name', 'sapi_in'])
  app.run(main)
