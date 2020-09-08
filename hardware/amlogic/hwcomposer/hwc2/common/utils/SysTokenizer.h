/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *  @author   Tellen Yu
 *  @version  1.0
 *  @date     2014/11/04
 *  @par function description:
 *  - 1 parse file use tokenizer
 */

#ifndef _SYS_TOKENIZER_H
#define _SYS_TOKENIZER_H

#include <assert.h>

#define MAX_STR_LEN         4096

/**
 * A simple tokenizer for loading and parsing ASCII text files line by line.
 */
class SysTokenizer {
public:
    SysTokenizer(const char*filename, char* buffer,
        bool ownBuffer, size_t length);

    ~SysTokenizer();

    /**
     * Opens a file and maps it into memory.
     *
     * Returns NO_ERROR and a tokenizer for the file, if successful.
     * Otherwise returns an error and sets outTokenizer to NULL.
     */
    static int open(const char*filename, SysTokenizer** outTokenizer);

    /**
     * Prepares to tokenize the contents of a string.
     *
     * Returns NO_ERROR and a tokenizer for the string, if successful.
     * Otherwise returns an error and sets outTokenizer to NULL.
     */
    static int fromContents(const char*filename,
            const char* contents, SysTokenizer** outTokenizer);

    /**
     * Returns true if at the end of the file.
     */
    inline bool isEof() const { return mCurrent == getEnd(); }

    /**
     * Returns true if at the end of the line or end of the file.
     */
    inline bool isEol() const { return isEof() || *mCurrent == '\n'; }

    /**
     * Gets the name of the file.
     */
    inline char* getFilename() const { return const_cast<char*>(mFilename); }

    /**
     * Gets a 1-based line number index for the current position.
     */
    inline int32_t getLineNumber() const { return mLineNumber; }

    /**
     * Formats a location string consisting of the filename and current line number.
     * Returns a string like "MyFile.txt:33".
     */
    char* getLocation() const;

    /**
     * Gets the character at the current position.
     * Returns null at end of file.
     */
    inline char peekChar() const { return isEof() ? '\0' : *mCurrent; }

    /**
     * Gets the remainder of the current line as a string, excluding the newline character.
     */
    char* peekRemainderOfLine() const;

    /**
     * Gets the character at the current position and advances past it.
     * Returns null at end of file.
     */
    inline char nextChar() { return isEof() ? '\0' : *(mCurrent++); }

    /**
     * Gets the next token on this line stopping at the specified delimiters
     * or the end of the line whichever comes first and advances past it.
     * Also stops at embedded nulls.
     * Returns the token or an empty string if the current character is a delimiter
     * or is at the end of the line.
     */
    char* nextToken(const char* delimiters);

    /**
     * Advances to the next line.
     * Does nothing if already at the end of the file.
     */
    void nextLine();

    /**
     * Skips over the specified delimiters in the line.
     * Also skips embedded nulls.
     */
    void skipDelimiters(const char* delimiters);

private:
    const char* mFilename;
    char* mBuffer;
    bool mOwnBuffer;
    size_t mLength;

    const char* mCurrent;
    int32_t mLineNumber;

    char mStrs[MAX_STR_LEN];
    char mLine[MAX_STR_LEN];
    inline const char* getEnd() const { return mBuffer + mLength; }

};

#endif // _SYS_TOKENIZER_H