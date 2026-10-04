# SPDX-License-Identifier: GPL-3.0-or-later
# Gaius -- cmake/embed_languages.cmake
#
# Writes <output> (a C++ file) holding every language file in lang/ (languages.txt and <code>.txt; not template.txt)
# as a byte array, so the program carries its translations itself and a player needs nothing beside gaius.exe.
# The files stay plain text in lang/ for translators; ui/embedded_lang.hpp is the interface. Included from the
# top-level CMakeLists.txt, which runs gaius_embed_languages().

function(gaius_embed_languages output)
  file(GLOB files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/lang/*.txt)
  list(SORT files)
  set(arrays "")
  set(table "")
  set(count 0)
  foreach(file ${files})
    get_filename_component(name ${file} NAME)
    if(name STREQUAL "template.txt")
      continue()
    endif()
    # A changed file means a new configure, and with it a new array.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
    file(READ ${file} hex HEX)
    string(REGEX REPLACE "(................................................)" "\\1\n" hex "${hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    math(EXPR count "${count} + 1")
    string(APPEND arrays "static const unsigned char kFile${count}[] = {\n${bytes}\n};\n\n")
    string(APPEND table "    {\"${name}\", kFile${count}, sizeof kFile${count}},\n")
  endforeach()

  set(text "// Written by cmake/embed_languages.cmake from lang/*.txt; do not edit.\n")
  string(APPEND text "#include \"ui/embedded_lang.hpp\"\n\nnamespace gaius::ui {\n\nnamespace {\n\n${arrays}}  // namespace\n\n")
  string(APPEND text "const EmbeddedLanguageFile kEmbeddedLanguageFiles[] = {\n${table}    {nullptr, nullptr, 0},\n};\n\n")
  string(APPEND text "const int kEmbeddedLanguageFileCount = ${count};\n\n}  // namespace gaius::ui\n")

  # configure_file only touches the output when its text changed, so an unchanged lang/ rebuilds nothing.
  set(staging ${output}.new)
  file(WRITE ${staging} "${text}")
  configure_file(${staging} ${output} COPYONLY)
  file(REMOVE ${staging})
endfunction()
