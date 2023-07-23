#include "llama.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <codecvt>
#include <map>
#include <vector>

static std::string escape_whitespace(const std::string& text) {
    std::string result;
    bool escaping = false;
    result += char(0xe2);
    result += char(0x96);
    result += char(0x81);
    for (size_t offs = 0; offs < text.length(); ++offs) {
        if (text[offs] == ' ') {
            if (!escaping) {
                result += char(0xe2);
                result += char(0x96);
                result += char(0x81);
                escaping = true;
            }
        }
        else {
            escaping = false;
            result += text[offs];
        }
    }
    return result;
}

static std::string unescape_whitespace(llama_context* ctx, const llama_token* tokens, int count) {
    std::string result;
    for (int i = 0; i < count; ++i) {
        result += llama_token_to_str(ctx, tokens[i]);
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <vocab-file>\n", argv[0]);
        return 1;
    }

    const std::string fname = argv[1];

    fprintf(stderr, "%s : reading vocab from: '%s'\n", __func__, fname.c_str());

    llama_model * model;
    llama_context * ctx;

    llama_backend_init(false);

    // load the vocab
    {
        auto lparams = llama_context_default_params();

        lparams.vocab_only = true;

        model = llama_load_model_from_file(fname.c_str(), lparams);

        if (model == NULL) {
            fprintf(stderr, "%s: error: failed to load vocab '%s'\n", __func__, fname.c_str());
            return 1;
        }

        ctx = llama_new_context_with_model(model, lparams);

        if (ctx == NULL) {
            fprintf(stderr, "%s: error: failed to load vocab '%s'\n", __func__, fname.c_str());
            llama_free_model(model);
            return 1;
        }
    }

    const int n_vocab = llama_n_vocab(ctx);

    if (n_vocab != 32000) {
        fprintf(stderr, "%s : expected 32000 tokens, got %d\n", __func__, n_vocab);
        llama_free_model(model);
        llama_free(ctx);
        return 2;
    }

    for (int i = 0; i < n_vocab; ++i) {
        std::string forward = llama_token_to_str_bpe(ctx, i);
        std::vector<llama_token> tokens(forward.length());
        int n = llama_tokenize_bpe(ctx, forward.c_str(), tokens.data(), forward.length(), false);
        if (n == 1) {
            if (i != tokens[0]) {
                std::string backward = llama_token_to_str(ctx, tokens[0]);
                fprintf(stderr, "%s : error: token %d is string %s but tokenize() returns token %d %s\n", 
                    __func__, i, llama_token_to_str(ctx, i).c_str(), tokens[0], backward.c_str());
            }
        } else {
            if (i <= 258) {
                fprintf(stderr, "%s : info: token %d is string %s and tokenize() returns tokens %s\n", 
                    __func__, i, llama_token_to_str(ctx, i).c_str(), unescape_whitespace(ctx, tokens.data(), n).c_str());
            } else {
                fprintf(stderr, "%s : error: token %d is string %s but tokenize() returns tokens %s\n", 
                    __func__, i, llama_token_to_str(ctx, i).c_str(), unescape_whitespace(ctx, tokens.data(), n).c_str());
            }
        }
    }

    std::wstring_convert<typename std::codecvt_utf8<wchar_t>, wchar_t> converter;
    for (wchar_t ch = 0x0000; ch < 0xffff; ++ch) {
        std::wstring wstr(1, ch);
        std::string str = converter.to_bytes(wstr);
        std::vector<llama_token> tokens(str.length() + 1);
        auto n = llama_tokenize(ctx, escape_whitespace(str).c_str(), tokens.data(), str.length() + 1, false);
        if (n == 1) {
            fprintf(stderr, "%s : info: %s tokenized to %d \n", 
                __func__, str.c_str(), tokens[0]);
        }
    }

    llama_free_model(model);
    llama_free(ctx);

    llama_backend_free();

    return 0;
}
