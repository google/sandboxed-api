// This is a jsonnet code which evaluates to mutliple output files.
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