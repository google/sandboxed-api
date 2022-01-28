// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This is jsonnet code which evaluates to json file, which can be
// interpreted as YAML stream.
local
  first_object = {
    name: 'First object\'s name.',
    age: 'Just created!',
  },
  second_object = {
    name: 'Hi, my name is <second_object>.',
    sibling: first_object.name
  };

[first_object, second_object]
