// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This is jsonnet code which evaluates to multiple output files.
{
  "first_file.json": {
    name: 'This is the first file created by the multiple-files example code.',
    caption: 'The other one\'s name is -> ' + $["second_file.json"].name,
  },
  "second_file.json": {
    name: 'And that is the other one.',
    caption: 'If it was the first one, variable name would hold what\'s in <first_name> variable.',
    first_name: $["first_file.json"].name,
  },
}
