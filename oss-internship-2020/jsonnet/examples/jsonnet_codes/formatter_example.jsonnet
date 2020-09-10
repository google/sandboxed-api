// This is a poorly written jsonnet file. Given to the formatter executable will be changed into a canonical jsonnet file form.
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
