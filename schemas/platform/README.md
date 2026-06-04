# Platform Resource Schemas

This directory contains full text XSD resources extracted from 1C platform
resource files. These files are platform evidence for the native ordinary-form
object model and value codecs.

The active baseline is `8.5`. Its manifest is `8.5/schemas.json`; it records
the resource module, namespace, file size, and SHA-256 hash for each schema.

Do not replace this layer with generated C++ snippets. Headers such as
`platform_form_schema.hpp` are compact native indexes generated from platform
evidence; the full resource schemas live here.

Do not add platform binaries, `.res`, `.hbk`, `.so`, `.dll`, archives, private
processors, or local extraction paths to this directory.
