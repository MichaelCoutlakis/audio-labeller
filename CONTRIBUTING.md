# Contributing

## Coding

- Use the std c++ coding convention (snake_case etc). See .clang-tidy
- Formatting defined via .clang-format
- Only use permissively licensed (non-copyleft) dependencies. Pulling a
  dependency must be worth the weight

## Compatibility before version 1

Until version 1 is released, file formats (project, palette, label file,
review lists and their results, app settings) may change without keeping the
old form readable. Don't add aliases for old names, readers of older versions
or migration code: change the format, the schema, the examples and the docs
together, and note the change in the CHANGELOG. Code kept for compatibility
grows into cases nobody uses and everyone has to read.

From version 1, a format change keeps older files readable, or says how to
convert them.
