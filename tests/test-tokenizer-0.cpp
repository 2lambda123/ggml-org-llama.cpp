#include "llama.h"

#include <cstdio>
#include <string>
#include <map>
#include <vector>

static const std::map<std::string, std::vector<llama_token>> & k_tests()
{
    static std::map<std::string, std::vector<llama_token>> _k_tests = {
        { "Hello World",        { 1,  10994,   2787, }, },
        { " Hello World",       { 1,  15043,   2787, }, },
        { " Hello World!",      { 1,  15043,   2787,  29991, }, },
        { " this is 🦙.cpp",    { 1,    445,    338,  29871,    243,    162,    169,    156,  29889,   8223, }, },
        { "w048 7tuijk dsdfhu", { 1,  29893,  29900,  29946,  29947,  29871,  29955,   9161,  13535,  18031,   2176,   6905, }, },
        { "нещо на Български",  { 1,    821,   4851,    665,   1386,  29713,   1305, }, },
        { "\xe6\x88\x91\xe4\xbb\xac\xe5\xa4\xa7\xe5\xae\xb6\xe4\xb8\x80\xe8\xb5\xb7",                 { 1, 30672, 31381, 30257, 30613, 30287, 31558, }, },
        { " >>>>ANSWER<<", {1, 5099, 6778, 2190, 23066, 1001, 9314}, },
    };
    return _k_tests;
};

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

    for (const auto & test_kv : k_tests()) {
        std::vector<llama_token> res(test_kv.first.size());
        const int n = llama_tokenize(ctx, test_kv.first.c_str(), res.data(), int(res.size()), true);
        res.resize(n);

        bool correct = res.size() == test_kv.second.size();

        for (int i = 0; i < (int) res.size() && correct; ++i) {
            if (res[i] != test_kv.second[i]) {
                correct = false;
            }
        }

        if (!correct) {
            fprintf(stderr, "%s : failed test: '%s'\n", __func__, test_kv.first.c_str());
            fprintf(stderr, "%s : expected tokens: ", __func__);
            for (const auto & t : test_kv.second) {
                fprintf(stderr, "%6d, ", t);
            }
            fprintf(stderr, "\n");
            for (const auto & t : test_kv.second) {
                fprintf(stderr, "%7s ", llama_token_to_str(ctx, t));
            }
            fprintf(stderr, "\n");
            fprintf(stderr, "%s : got tokens:      ", __func__);
            for (const auto & t : res) {
                fprintf(stderr, "%6d, ", t);
            }
            fprintf(stderr, "\n");
            for (const auto & t : res) {
                fprintf(stderr, "%7s ", llama_token_to_str(ctx, t));
            }
            fprintf(stderr, "\n");

            llama_free_model(model);
            llama_free(ctx);
            return 3;
        }
    }

#if 0
    // how many tokens would not tokenize to themselves
    for (llama_token i = 1; i < llama_n_vocab(ctx); i++)
    {
        const char* str = llama_token_to_str(ctx, i);
        std::vector<llama_token> res(100);

        const int n = llama_tokenize(ctx, str, res.data(), int(res.size()), false);
        res.resize(n);

        for (const auto & t : res)
        {
            //if (t == 1) continue;

            if (t != i) {
                fprintf(stderr, "%s : failed test: '%s'\n", __func__, str);
                fprintf(stderr, "%s : expected tokens: %d\n", __func__, i);
                fprintf(stderr, "%s : got tokens:      ", __func__);
                for (const auto & t : res) {
                    fprintf(stderr, "%6d, ", t);
                }
                for (const auto & t : res) {
                    fprintf(stderr, "%s|", llama_token_to_str(ctx, t));
                }

                fprintf(stderr, "\n");
            }
        }

    }
#endif

    llama_free_model(model);
    llama_free(ctx);

    llama_backend_free();

    return 0;
}
