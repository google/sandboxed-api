// This is a jsonnet code which evaluates to json file, which can be interpreted as YAML stream.
local
  first_object = {
    name: 'First object\'s name.',
    age: 'Just created!,
  },
  second_object = {
    name: `Hi, my name is <second_object>.`,
    sibling: first_object.name
  };

[first_object, second_object]