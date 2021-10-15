#include "decoder.h"
#include "queue_buf.h"
#include "linked_list.h"
#include "common/math_util.h"
#include "lightanchor.h"
#include "lightanchor_detector.h"

// #define DEBUG

static inline uint8_t cyclic_lsl(uint8_t bits, uint8_t size)
{
    return (bits << 1) | ((bits >> (size - 1)) & 0x1);
}

// size_t hamming_dist(size_t a, size_t b)
// {
//     size_t c = a ^ b;
//     unsigned int hamm = 0;
//     while (c)
//     {
//         ++hamm;
//         c &= (c-1);
//     }
//     return hamm;
// }

static int match(uint8_t a, uint8_t b, uint8_t *match)
{
    if (a == b)
    {
        if (match) *match = a;
        return 1;
    }
    else {
        if (match) *match = 0xff; // invalid
        return 0;
    }
}

int lightanchor_decode(lightanchor_detector_t *ld, lightanchor_t *candidate_curr)
{
#ifdef DEBUG
    printf(""BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN" ",
            BYTE_TO_BINARY(candidate_curr->code>>8), BYTE_TO_BINARY(candidate_curr->code));
#endif

    uint8_t code_to_match;
    if (candidate_curr->valid)
    {
        code_to_match = candidate_curr->next_code;
        uint8_t shifted = cyclic_lsl(code_to_match, 8);
#ifdef DEBUG
        printf(" "BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN"\n",
                BYTE_TO_BINARY(code_to_match>>8), BYTE_TO_BINARY(code_to_match));
#endif
        if (match(candidate_curr->code, code_to_match, NULL))
        {
            candidate_curr->next_code = cyclic_lsl(code_to_match, 8);
            candidate_curr->valid = 1;
        }
        // additional check in case code is matched to shifted version of itself
        else if (match(candidate_curr->code, shifted, NULL))
        {
            candidate_curr->next_code = cyclic_lsl(shifted, 8);
            candidate_curr->valid = 1;
        }
        else {
#ifdef DEBUG
            printf("==== LOST ====\n");
            printf(""BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN" != ",
                    BYTE_TO_BINARY(candidate_curr->code>>8), BYTE_TO_BINARY(candidate_curr->code));
            printf(""BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN" | ",
                    BYTE_TO_BINARY(code_to_match>>8), BYTE_TO_BINARY(code_to_match));
            printf(""BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN"\n",
                    BYTE_TO_BINARY(shifted>>8), BYTE_TO_BINARY(shifted));

            int j = candidate_curr->brightnesses.idx;
            for (int i = 0; i < BUF_SIZE; i++)
            {
                printf("%u ", candidate_curr->brightnesses.buf[j]);
                j = (j + 1) % BUF_SIZE;
            }
            puts("");
            printf("===============\n");
#endif
            candidate_curr->valid = 0;
            // candidate_curr->next_code = cyclic_lsl(code_to_match, 8);
        }
        return candidate_curr->valid;
    }
    else {
        for (int i = 0; i < zarray_size(ld->codes); i++)
        {
            glitter_code_t *code;
            zarray_get_volatile(ld->codes, i, &code);
            code_to_match = code->code;
#ifdef DEBUG
            printf(" "BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN"\n",
                    BYTE_TO_BINARY(code_to_match>>8), BYTE_TO_BINARY(code_to_match));
#endif
            if (match(candidate_curr->code, code_to_match, &candidate_curr->match_code))
            {
#ifdef DEBUG
                printf("==== MATCH ====\n");
                printf(""BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN" == ",
                        BYTE_TO_BINARY(candidate_curr->code>>8), BYTE_TO_BINARY(candidate_curr->code));
                printf(""BYTE_TO_BINARY_PATTERN""BYTE_TO_BINARY_PATTERN"\n",
                        BYTE_TO_BINARY(code_to_match>>8), BYTE_TO_BINARY(code_to_match));
                printf("===============\n");
#endif
                candidate_curr->code = code_to_match;
                candidate_curr->next_code = cyclic_lsl(code_to_match, 8);
                candidate_curr->valid = 1;
                return 1;
            }
        }
        candidate_curr->valid = 0;
        return 0;
    }
}
