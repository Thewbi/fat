// cd /Users/bischowg/dev/osdev/fat/src
// gcc -c fat.c
// gcc -c test_fat.c -I/Users/bischowg/Downloads/cmocka-1.1.5/include
// gcc -o "testfat" ./test_fat.o ./fat.o -lcmocka
// rm fat.o test_fat.o testfat

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "fat.h"

static void test_filename()
{
    char buffer[12];

    // test.txt --> TEST.TXT
    filenameToFatElevenThree("test.txt", buffer);
    assert_string_equal(buffer, "TEST    .TXT");

    // test.po --> TEST.PO
    filenameToFatElevenThree("test.po", buffer);
    assert_string_equal(buffer, "TEST    .PO");

    // if a filename contains illegal characters it has to be modified,
    //   - leading dots, spaces and superfluous dots are erased
    //   - plus-signs and other special characters are replaced by underscore
    // if a filename was modified in any way, a numeric truncate follows no matter if the filename has a valid length or not
    // if a filename is valid but merely to long, it is numerically truncated.

    // if there are more than one dot, only the last dot survives and the others are replaced by underscore
    // longer filenames than eight characters are truncated
    //
    // TextFile.Mine.txt --> TEXTFI~1.TXT

    // extension with more than three characters are truncated
    // if there are more than one dot, only the last dot survives and the others are replaced by underscore
    // spaces are replaced by underscore
    // plus is replaced by underscore
    //
    // ver +1.2.text --> VER_12~1.TEX

    // leading dots are erased
    //
    // .bashrc.swp --> BASHRC~1.SWP

    // algorithm:
    //
    // provide two buffers, one for the extension (3 chars), one for the filename (8 chars)
    // initialize both buffers with space characters
    // provide a flag called modified, initial value false
    //
    // find the position of the rightmost dot in the input string (= last dot index)
    // if there is none, set the last dot index to the length of the string
    //
    // from left to right, got through the filename character by character until the last dot index
    // look at each character
    //   - dots and spaces are erased (= not copied into the firstname buffer), set the modified flag to true
    //   - special characters (plus and others) are replaced by underscores and copied to the firstname buffer, set the modified flag to true
    //   - Convert lowercase characters to uppercase but that does not set the modified flag to true
    //
    // If the firstname buffer is full (max 8. characters) set the modified flag to true and ignore all subsequent
    // characters until the last dot index is reached. Now begin to fill the extension buffer using the same algorithm but
    // with max 3 characters
    //
    // if the modified flag is set, apply a numerical truncation algorithm to the firstname buffer
    //
    // using the firstname and the lastname buffer, fill in the 12 character buffer
    // the filname is leftalign, right-padded with spaces to 8 characters
    // the extension is right aligned, right-padded with spaced to 3 characters
    // a dot is written to the left of the extension

    // algorithm - numerical truncate
    // the numerical truncate will right append tilde and a number
    // if the filename is already 8 characters, numerical truncate will overide parts of the filename to insert tilde and number
}

int main(int argc, char **argv)
{
    const UnitTest tests[] =
        {
            unit_test(test_filename),
        };

    return run_tests(tests);
}