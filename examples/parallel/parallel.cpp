// A basic application simulating a server with multiple clients.
// The clients submite requests to the server and they are processed in parallel.

#include "build-info.h"

#include "common.h"
#include "llama.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// trim whitespace from the beginning and end of a string
static std::string trim(const std::string & str) {
    size_t start = 0;
    size_t end = str.size();

    while (start < end && isspace(str[start])) {
        start += 1;
    }

    while (end > start && isspace(str[end - 1])) {
        end -= 1;
    }

    return str.substr(start, end - start);
}

static std::string k_system =
R"(Transcript of a never ending dialog, where the User interacts with an Assistant.
The Assistant is helpful, kind, honest, good at writing, and never fails to answer the User's requests immediately and with precision.

User: Recommend a nice restaurant in the area.
Assistant: I recommend the restaurant "The Golden Duck". It is a 5 star restaurant with a great view of the city. The food is delicious and the service is excellent. The prices are reasonable and the portions are generous. The restaurant is located at 123 Main Street, New York, NY 10001. The phone number is (212) 555-1234. The hours are Monday through Friday from 11:00 am to 10:00 pm. The restaurant is closed on Saturdays and Sundays.
User: Who is Richard Feynman?
Assistant: Richard Feynman was an American physicist who is best known for his work in quantum mechanics and particle physics. He was awarded the Nobel Prize in Physics in 1965 for his contributions to the development of quantum electrodynamics. He was a popular lecturer and author, and he wrote several books, including "Surely You're Joking, Mr. Feynman!" and "What Do You Care What Other People Think?".
User:)";

static std::vector<std::string> k_prompts = {
    "What is the meaning of life?",
    "Tell me an interesting fact about llamas.",
    "What is the best way to cook a steak?",
    "Are you familiar with the Special Theory of Relativity and can you explain it to me?",
    "Recommend some interesting books to read.",
    "What is the best way to learn a new language?",
    "How to get a job at Google?",
    "If you could have any superpower, what would it be?",
    "I want to learn how to play the piano.",
};

struct client {
    int32_t id = 0;

    llama_seq_id seq_id = -1;

    llama_token sampled;

    int64_t t_start_prompt;
    int64_t t_start_gen;

    int32_t n_prompt  = 0;
    int32_t n_decoded = 0;
    int32_t i_batch   = -1;

    std::string input;
    std::string prompt;
    std::string response;

    std::vector<llama_token> tokens_prev;
};

int main(int argc, char ** argv) {
    srand(1234);

    gpt_params params;

    if (gpt_params_parse(argc, argv, params) == false) {
        return 1;
    }

    // number of simultaneous "clients" to simulate
    const int32_t n_clients = params.n_parallel;

    // requests to simulate
    const int32_t n_seq = params.n_sequences;

    // insert new requests as soon as the previous one is done
    const bool hot_plug = params.hot_plug;

#ifndef LOG_DISABLE_LOGS
    log_set_target(log_filename_generator("parallel", "log"));
    LOG_TEE("Log start\n");
    log_dump_cmdline(argc, argv);
#endif // LOG_DISABLE_LOGS

    // init llama.cpp
    llama_backend_init(params.numa);

    llama_model * model = NULL;
    llama_context * ctx = NULL;

    // load the target model
    params.logits_all = true;
    std::tie(model, ctx) = llama_init_from_gpt_params(params);

    fprintf(stderr, "\n\n");
    fflush(stderr);

    const int n_ctx   = llama_n_ctx(ctx);
    const int n_vocab = llama_n_vocab(ctx);

    std::vector<client> clients(n_clients);
    for (size_t i = 0; i < clients.size(); ++i) {
        auto & client = clients[i];
        client.id = i;
        client.tokens_prev.resize(n_ctx);
        std::fill(client.tokens_prev.begin(), client.tokens_prev.end(), 0);
    }

    std::vector<llama_token_data> candidates;
    candidates.reserve(n_vocab);

    std::vector<llama_token> tokens_system;
    tokens_system = ::llama_tokenize(ctx, k_system, true);
    const uint32_t n_tokens_system = tokens_system.size();

    llama_seq_id g_seq_id = 0;

    std::vector<llama_token>  batch_token;
    std::vector<llama_pos>    batch_pos;
    std::vector<llama_seq_id> batch_seq_id;
    std::vector<int8_t>       batch_logits;
    std::vector<client *>     batch_clients;

    int32_t n_total_prompt = 0;
    int32_t n_total_gen    = 0;
    int32_t n_cache_miss   = 0;

    const auto t_main_start = ggml_time_us();

    LOG_TEE("%s: Simulating parallel requests from clients:\n", __func__);
    LOG_TEE("%s: n_parallel = %d, n_sequences = %d, hot_plug = %d, system tokens = %d\n", __func__, n_clients, n_seq, hot_plug, n_tokens_system);
    LOG_TEE("\n");

    {
        LOG_TEE("%s: Evaluating the system prompt ...\n", __func__);

        batch_pos.clear();
        batch_seq_id.clear();

        for (size_t i = 0; i < n_tokens_system; ++i) {
            batch_pos.push_back(i);
            batch_seq_id.push_back(0);
        }

        llama_batch batch = {
            n_tokens_system,
            tokens_system.data(),
            nullptr,
            batch_pos.data(),
            batch_seq_id.data(),
            nullptr,
            0, 0, 0, // unused
        };

        if (llama_decode(ctx, batch, params.n_threads) != 0) {
            LOG_TEE("%s: llama_decode() failed\n", __func__);
            return 1;
        }

        // assign the system KV cachce to all parallel sequences
        for (int32_t i = 1; i < n_clients; ++i) {
            llama_kv_cache_seq_cp(ctx, 0, i, 0, n_tokens_system);
        }

        LOG_TEE("\n");
    }

    LOG_TEE("Processing requests ...\n\n");

    while (true) {
        uint32_t n_tokens = 0;

        batch_token.clear();
        batch_pos.clear();
        batch_seq_id.clear();
        batch_logits.clear();

        for (auto & client : clients) {
            if (client.seq_id == -1) {
                continue;
            }

            batch_token.push_back(client.sampled);
            batch_pos.push_back(n_tokens_system + client.n_prompt + client.n_decoded);
            batch_seq_id.push_back(client.id);
            batch_logits.push_back(true);
            batch_clients.push_back(&client);
            client.n_decoded += 1;
            client.i_batch = batch_token.size() - 1;
        }

        if (batch_token.empty()) {
            // all sequences have ended - clear the entire KV cache
            for (int i = 0; i < n_clients; ++i) {
                llama_kv_cache_seq_rm(ctx, i, n_tokens_system, -1);
            }
        }

        if (hot_plug || batch_token.empty()) {
            for (auto & client : clients) {
                if (client.seq_id == -1 && g_seq_id < n_seq) {
                    client.seq_id = g_seq_id;
                    client.t_start_prompt = ggml_time_us();
                    client.t_start_gen    = 0;

                    client.input = k_prompts[rand() % k_prompts.size()];
                    client.prompt = client.input + "\nAssistant:";
                    client.response = "";
                    std::fill(client.tokens_prev.begin(), client.tokens_prev.end(), 0);

                    std::vector<llama_token> tokens_prompt;
                    tokens_prompt = ::llama_tokenize(ctx, client.prompt, true);

                    for (size_t i = 0; i < tokens_prompt.size(); ++i) {
                        batch_token.push_back(tokens_prompt[i]);
                        batch_pos.push_back(i + n_tokens_system);
                        batch_seq_id.push_back(client.id);
                        batch_clients.push_back(&client);
                        batch_logits.push_back(false);
                    }
                    batch_logits.back() = true;

                    client.n_prompt  = tokens_prompt.size();
                    client.n_decoded = 0;
                    client.i_batch   = batch_token.size() - 1;

                    g_seq_id += 1;
                    if (hot_plug) {
                        //break;
                    }
                }
            }
        }

        if (batch_token.empty()) {
            break;
        }

        // process in chunks of params.n_batch
        int32_t n_batch = params.n_batch;

        for (int32_t i = 0; i < (int32_t) batch_token.size(); i += n_batch) {
            n_tokens = std::min(n_batch, (int32_t) (batch_token.size() - i));

            llama_batch batch = {
                n_tokens,
                batch_token.data() + i,
                nullptr,
                batch_pos.data() + i,
                batch_seq_id.data() + i,
                batch_logits.data() + i,
                0, 0, 0, // unused
            };

            const int ret = llama_decode(ctx, batch, params.n_threads);
            if (ret != 0) {
                if (n_batch == 1 || ret < 0) {
                    LOG_TEE("%s : failed to decode batch, n_batch = %d, ret = %d\n", __func__, n_batch, ret);
                    return 1;
                }

                LOG("%s : failed to decode batch, retrying with n_batch = %d\n", __func__, n_batch / 2);

                n_cache_miss += 1;

                // retry with half the batch size to try to find a free slot in the KV cache
                n_batch /= 2;
                i -= n_batch;

                continue;
            }

            LOG("%s : decoded batch of %d tokens\n", __func__, n_tokens);

            for (auto & client : clients) {
                if (client.i_batch < (int) i || client.i_batch >= (int) (i + n_tokens)) {
                    continue;
                }

                //printf("client %d, seq %d, token %d, pos %d, batch %d\n",
                //        client.id, client.seq_id, client.sampled, client.n_decoded, client.i_batch);

                const llama_token id = llama_sample_token(ctx, NULL, NULL, params, client.tokens_prev, candidates, client.i_batch - i);

                if (client.n_decoded == 1) {
                    // start measuring generation time after the first token to make sure all concurrent clients
                    // have their prompt already processed
                    client.t_start_gen = ggml_time_us();
                }

                // remember which tokens were sampled - used for repetition penalties during sampling
                client.tokens_prev.erase(client.tokens_prev.begin());
                client.tokens_prev.push_back(id);

                const std::string token_str = llama_token_to_piece(ctx, id);
                client.response += token_str;
                client.sampled = id;

                //printf("client %d, seq %d, token %d, pos %d, batch %d: %s\n",
                //        client.id, client.seq_id, id, client.n_decoded, client.i_batch, token_str.c_str());

                if (client.n_decoded > 2 &&
                        (id == llama_token_eos(ctx) || client.n_decoded + client.n_prompt >= params.n_predict ||
                         client.response.find("User:") != std::string::npos ||
                         client.response.find('\n') != std::string::npos)) {
                    // basic reverse prompt
                    const size_t pos = client.response.find("User:");
                    if (pos != std::string::npos) {
                        client.response = client.response.substr(0, pos);
                    }

                    // delete only the generated part of the sequence, i.e. keep the system prompt in the cache
                    llama_kv_cache_seq_rm(ctx, client.id, n_tokens_system, n_ctx);

                    const auto t_main_end = ggml_time_us();

                    LOG_TEE("\033[1mClient %3d, seq %4d, prompt %4d t, response %4d t, time %5.2f s, speed %5.2f t/s, cache miss %d \033[0m \n\nInput:    %s\nResponse: %s\n\n",
                            client.id, client.seq_id, client.n_prompt, client.n_decoded,
                            (t_main_end - client.t_start_prompt) / 1e6,
                            (double) (client.n_prompt + client.n_decoded) / (t_main_end - client.t_start_prompt) * 1e6,
                            n_cache_miss,
                            ::trim(client.input).c_str(),
                            ::trim(client.response).c_str());

                    n_total_prompt += client.n_prompt;
                    n_total_gen    += client.n_decoded;

                    client.seq_id = -1;
                }

                client.i_batch = -1;
            }
        }
    }

    const auto t_main_end = ggml_time_us();

    LOG_TEE("\n\n");
    LOG_TEE("Total prompt tokens: %6d, speed: %5.2f t/s\n", n_total_prompt, (double) (n_total_prompt              ) / (t_main_end - t_main_start) * 1e6);
    LOG_TEE("Total gen tokens:    %6d, speed: %5.2f t/s\n", n_total_gen,    (double) (n_total_gen                 ) / (t_main_end - t_main_start) * 1e6);
    LOG_TEE("Total speed (AVG):   %6s  speed: %5.2f t/s\n", "",             (double) (n_total_prompt + n_total_gen) / (t_main_end - t_main_start) * 1e6);
    LOG_TEE("Cache misses:        %6d\n", n_cache_miss);

    LOG_TEE("\n\n");

    llama_print_timings(ctx);

    llama_free(ctx);
    llama_free_model(model);

    llama_backend_free();

    fprintf(stderr, "\n\n");

    return 0;
}
