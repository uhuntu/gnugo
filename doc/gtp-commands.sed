# gtp-commands.sed (edit in -*-shell-script-*- mode)
# Author: Thien-Thi Nguyen <ttn@gnu.org>

# Look for function headers.
/\* Function: /,/^{/!d

# Remove cruft.
/^static int/d
/^{/d
/^ \*\//d
s/(char.*)//g

# Hold comment lines, deleting them from pattern space for now.
/.\*/{
 s/^..//
 s/^ //
 H
 d
}

# When we see the function name, merge hold space, in the process
# generating proper texinfo @cindex, @item and @verbatim formatting.
# As a bonus, the `Function' field is moved to the @item line.
# We use repeated `x' commands instead of the simpler `i' to avoid
# requiring a `d' (which would render this script non-composable).
/^gtp_/{
 s/\(.*\)/@cindex \1\
@item \1/
 x
 s/^\(.\)Function: *\(.*\)\(Arguments:\)/: \2@verbatim\1\3/
 s/\
 *\(.*.@verbatim\)/ \1/g
 s/$/\
@end verbatim/
 H
 s/.*//
 x
 s/\
//2
}

# gtp-commands.sed ends here
