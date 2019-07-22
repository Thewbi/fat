#include "fat.h"

// truncates after maxlength
void numericalTruncate(char *out, char *input, int outLen, int maxLength)
{
    size_t textlen = strlen(input);

    // amount of characters to write into the output buffer
    size_t fillSize = maxLength < outLen ? maxLength : outLen;

    size_t startOfPostfix = textlen <= fillSize - 2 ? textlen : fillSize - 2;

    for (int i = 0; i < startOfPostfix; i++)
    {
        out[i] = input[i];
    }
    out[startOfPostfix] = '~';
    out[startOfPostfix + 1] = '1';

    for (int i = startOfPostfix + 2; i < outLen; i++)
    {
        out[i] = '\0';
    }
    /*size_t min = textlen < outBufferLen ? textlen : outBufferLen;
    min = min < maxLength ? min : maxLength;

    for (int i = 0; i < outBufferLen; i++)
    {
        if (i < min - 2)
        {
            out[i] = input[i];
        }
        else if (i < min - 1)
        {
            out[i] = '~';
        }
        else if (i < min)
        {
            out[i] = '1';
        }
        else
        {
            out[i] = '\0';
        }
    }
     */
}

void to_upper(char *out, char *input, int outBufferLen)
{
    for (int i = 0; i < outBufferLen; i++)
    {
        char c = input[i];
        if (isalpha(c))
        {
            out[i] = c;
            // if this is a lowercase character, turn it into uppercase
            if ((out[i] > 96) && (out[i] < 123))
            {
                out[i] ^= 0x20;
            }
        }
    }
}

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

// TODO:
// method trim_start(const char *filename, char *out, list of characters to trim)
// method trim_end(const char *filename, char *out, list of characters to trim)
// method to_uppercase(const char *filename)
void filenameToFatElevenThree(const char *filename, char *out, int outLen)
{
    memset(out, ' ', outLen);

    // parent folder
    if (strlen(filename) == 2 && strcmp(filename, "..") == 0)
    {
        memset(out, '\0', outLen);
        out[0] = out[1] = '.';
        return;
    }

    // current folder
    if (strlen(filename) == 1 && strcmp(filename, ".") == 0)
    {
        memset(out, '\0', outLen);
        out[0] = '.';
        return;
    }

    unsigned int stringLength = strlen(filename);
    if (stringLength == 0)
    {
        return;
    }

    // allocate buffer and initialize with zero
    char *newBuffer = (char *)malloc(sizeof(char) * stringLength);
    if (newBuffer == NULL)
    {
        return;
    }
    memset(newBuffer, 0, stringLength);

    // trim leading space and dots
    unsigned char currentChar = *filename;
    while (isspace(currentChar) || currentChar == '.')
    {
        filename++;
        currentChar = *filename;
    }
    if (*filename == 0) // All spaces?
    {
        return;
    }

    // trim trailing space
    const char *end = filename + strlen(filename) - 1;
    while (end > filename && (isspace((unsigned char)*end)))
    {
        end--;
    }
    end++;

    // set output size to minimum of trimmed string length and buffer size minus 1
    size_t out_size = (end - filename) < stringLength ? (end - filename) : stringLength;

    // copy trimmed string and add null terminator
    memcpy(newBuffer, filename, out_size);
    newBuffer[out_size] = 0;

    // if there is no extension, return
    if (newBuffer[out_size - 1] == '.')
    {
        // TODO, to uppercase
        memcpy(out, newBuffer, outLen);
        if (out_size >= 8)
        {
            numericalTruncate(out, newBuffer, outLen, 8);
        }

        to_upper(out, out, out_size);

        return;
    }

    char fnbuffer[8];
    char extbuffer[3];
    bool modified = false;

    memset(fnbuffer, ' ', sizeof(fnbuffer));
    memset(extbuffer, ' ', sizeof(extbuffer));

    // find last dot in filename
    stringLength = strlen(newBuffer);
    int lastDotIndex = stringLength;
    char *chptr = strrchr(newBuffer, '.');
    if (chptr != 0)
    {
        lastDotIndex = chptr - newBuffer;
    }

    // convert and fill firstname buffer and extension buffer
    int fnIndex = 0;
    for (int i = 0; i < lastDotIndex; i++)
    {
        char c = newBuffer[i];

        if (isalpha(c))
        {
            fnbuffer[fnIndex] = c;
            // if this is a lowercase character, turn it into uppercase
            if ((fnbuffer[fnIndex] > 96) && (fnbuffer[fnIndex] < 123))
            {
                fnbuffer[fnIndex] ^= 0x20;
            }
            fnIndex++;
        }
        // replace certain characters by underscore
        else if (c == '+')
        {
            fnbuffer[fnIndex] = '_';
            fnIndex++;
            modified = true;
        }
        // ignore certain characters
        else if (c == '.' || c == ' ')
        {
            modified = true;
        }
        else
        {
            fnbuffer[fnIndex] = c;
            fnIndex++;
        }

        // firstname buffer is full (max 8 characters), break the loop
        if (fnIndex >= 8)
        {
            // if the firstname was not fully captured, it has to be numerically truncated, modified is set to true
            if (lastDotIndex > 8)
            {
                modified = true;
            }
            break;
        }
    }

    if (modified)
    {
        numericalTruncate(fnbuffer, fnbuffer, sizeof(fnbuffer), 8);
    }

    // convert and fill firstname buffer and extension buffer
    int extIndex = 0;
    for (int i = lastDotIndex + 1; i <= stringLength; i++)
    {
        char c = filename[i];

        if (isalpha(c))
        {
            extbuffer[extIndex] = c;
            // if this is a lowercase character, turn it into uppercase
            if ((extbuffer[extIndex] > 96) && (extbuffer[extIndex] < 123))
            {
                extbuffer[extIndex] ^= 0x20;
            }
            extIndex++;
        }
        // replace certain characters by underscore
        else if (c == '+')
        {
            extbuffer[extIndex] = '_';
            extIndex++;
        }
        // ignore certain characters
        else if (c == '.' || c == ' ' || c == '\0')
        {
            ;
        }
        else
        {
            extbuffer[extIndex] = c;
            extIndex++;
        }

        // extension buffer is full (max 3 characters), break the loop
        if (extIndex >= 3)
        {
            break;
        }
    }

    /*
    for (int i = 0; i < 12; i++)
    {
        if (i < 8)
        {
            out[i] = fnbuffer[i];
        }
        else if (i == 8)
        {
            // only add a dot if there is an extension in the filename
            if (lastDotIndex == stringLength)
            {
                continue;
            }
            //out[i] = '.';
        }
        else
        {
            out[i] = extbuffer[i - 9];
        }
    }
*/

    for (int i = 0; i < 11; i++)
    {
        if (i < 8)
        {
            out[i] = fnbuffer[i];
        }
        else
        {
            out[i] = extbuffer[i - 8];
        }
    }

    free(newBuffer);
}
