# catalog.json as a C++ string literal (bridge.cpp includes it): split into raw-string pieces small enough for any
# compiler, none of which can contain the delimiter
file(READ "${IN}" text)
string(LENGTH "${text}" n)
set(out "")
set(i 0)
while(i LESS n)
    string(SUBSTRING "${text}" ${i} 8000 piece)
    string(APPEND out "R\"CATALOG(${piece})CATALOG\"\n")
    math(EXPR i "${i} + 8000")
endwhile()
file(WRITE "${OUT}" "${out}")
