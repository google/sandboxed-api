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

// This is a poorly written jsonnet file. Given to the formatter executable
// will be changed into a canonical jsonnet file form.
local b = import "somefile.libsonnet";  # comment
local a = import "differentfile.libsonnet";             // another comment in different style

local SomeStuff = {bar: "foo"};

            local funtion_to_do_addition(x,y)=x+y;

            {
"this": ((3)) ,
"that that":
funtion_to_do_addition(4,2),
arrArr: [[
  1, 2, 5
  ],
  3, 10,    19
  ]
} + SomeStuff
