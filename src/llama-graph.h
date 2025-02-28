#pragma once

#include <cstdint>
#include <vector>
#include <memory>

// note: do not add high-level objects here, such as llama_context, llama_kv_cache, etc.
//       not sure about llama_batch/llama_sbatch yet

struct ggml_cgraph;
struct ggml_context;
struct ggml_tensor;
struct ggml_backend_buffer;

struct llama_ubatch;

enum llama_graph_type {
    LLAMA_GRAPH_TYPE_DEFAULT,
    LLAMA_GRAPH_TYPE_ENCODER,
    LLAMA_GRAPH_TYPE_DECODER,
};

//
// llama_graph_input
//

class llama_graph_input_i {
public:
    virtual ~llama_graph_input_i() = default;

    virtual void set_input(const llama_ubatch * ubatch) = 0;

    // by default, we produce a single input tensor, but some children could produce more
    ggml_tensor * cur = nullptr;
};

using llama_graph_input_ptr = std::shared_ptr<llama_graph_input_i>;

class llama_graph_input_attn_i : public llama_graph_input_i {
public:
    virtual ~llama_graph_input_attn_i() = default;

    virtual ggml_tensor * get_kq_mask();
    virtual ggml_tensor * get_kq_mask_swa();
    virtual ggml_tensor * get_kq_mask_cross();
};

using llama_graph_input_attn_ptr = std::shared_ptr<llama_graph_input_attn_i>;

//
// llama_graph_result
//

class llama_graph_result_i {
public:
    virtual ~llama_graph_result_i() = default;

    virtual ggml_tensor * get_logits()      = 0;
    virtual ggml_tensor * get_embd()        = 0;
    virtual ggml_tensor * get_embd_pooled() = 0;

    virtual void set_inputs(const llama_ubatch * ubatch) = 0;
};

using llama_graph_result_ptr = std::unique_ptr<llama_graph_result_i>;

class llama_graph_result : public llama_graph_result_i {
public:
    llama_graph_result()          = default;
    virtual ~llama_graph_result() = default;

    ggml_tensor * get_logits()      override { return t_logits; }
    ggml_tensor * get_embd()        override { return t_embd; }
    ggml_tensor * get_embd_pooled() override { return t_embd_pooled; }

    void set_inputs(const llama_ubatch * ubatch) override {
        for (auto & input : inputs) {
            input->set_input(ubatch);
        }
    }

    void add_input(llama_graph_input_ptr input) {
        inputs.emplace_back(std::move(input));
    }

    // important graph nodes
    ggml_tensor * t_logits      = nullptr;
    ggml_tensor * t_embd        = nullptr;
    ggml_tensor * t_embd_pooled = nullptr;

    std::vector<llama_graph_input_ptr> inputs;
};

//
// llama_graph
//

// note: keep all methods const
// TODO: can become more granular in the future
class llama_graph_i {
public:
    llama_graph_i(llama_graph_type type);
    virtual ~llama_graph_i() = default;

    llama_graph_type get_type() const {
        return type;
    }

private:
    llama_graph_type type;

public:
    virtual int32_t get_n_outputs() const = 0;

    // callback that allows us to apply custom logic to each tensor (e.g. ggml-alloc, offloading, etc.)
    virtual void build_cb(
             ggml_tensor * cur,
              const char * name,
      const llama_ubatch & ubatch,
                     int   il) const = 0;

    // apply control vector for layer il
    virtual ggml_tensor * build_cvec(
            ggml_context * ctx0,
             ggml_tensor * cur,
                     int   il) const = 0;

    // do mat_mul, while optionally apply lora
    virtual ggml_tensor * build_lora_mm(
            ggml_context * ctx0,
             ggml_tensor * w,
             ggml_tensor * cur) const = 0;

    // do mat_mul_id, while optionally apply lora
    virtual ggml_tensor * build_lora_mm_id(
            ggml_context * ctx0,
             ggml_tensor * w,   // struct ggml_tensor * as
             ggml_tensor * cur, // struct ggml_tensor * b
             ggml_tensor * ids) const = 0;

    // rope factors based on the current context size
    virtual ggml_tensor * build_rope_factors(int il) const = 0;

    // graph build API (context-specific)

    // input embeddings with optional lora
    virtual llama_graph_input_ptr build_inp_embd(
            ggml_context * ctx0,
             ggml_tensor * tok_embd,
      const llama_ubatch & ubatch) const = 0;

    // enc-dec pos
    virtual llama_graph_input_ptr build_inp_pos_bucket(
            ggml_context * ctx0,
                 int32_t   n_tokens) const = 0;

    //
    // attention API
    //

    virtual llama_graph_input_attn_ptr build_attn_inp(
            ggml_context * ctx0,
                 int32_t   n_tokens,
                    bool   causal,
                    bool   swa) const = 0;

    virtual ggml_tensor * build_attn(
            llama_graph_input_attn_i * inp,
            ggml_context * ctx0,
             ggml_cgraph * gf,
             ggml_tensor * q_cur,
             ggml_tensor * k_cur,
             ggml_tensor * v_cur,
             ggml_tensor * kq_b,
                   float   kq_scale,
                     int   il) const;

    virtual ggml_tensor * build_attn_cross(
            llama_graph_input_attn_i * inp,
            ggml_context * ctx0,
             ggml_cgraph * gf,
             ggml_tensor * q_cur,
             ggml_tensor * k_cur,
             ggml_tensor * v_cur,
             ggml_tensor * kq_b,
                 float     kq_scale,
                 int       il) const;

    virtual llama_graph_input_ptr build_inp_cross_embd(
            ggml_context * ctx0) const;

    //
    // recurrent API
    //

    virtual llama_graph_input_ptr build_inp_s_copy(
            ggml_context * ctx0) const;

    virtual llama_graph_input_ptr build_inp_s_mask(
            ggml_context * ctx0) const;

    virtual ggml_tensor * build_copy_mask_state(
            ggml_context * ctx0,
             ggml_cgraph * gf,
             ggml_tensor * s,
             ggml_tensor * state_copy,
             ggml_tensor * state_mask,
                 int32_t   n_state,
                 int32_t   n_seqs) const;

    virtual ggml_tensor * build_mamba_layer(
            ggml_context * ctx0,
             ggml_cgraph * gf,
             ggml_tensor * cur,
             ggml_tensor * state_copy,
             ggml_tensor * state_mask,
      const llama_ubatch & ubatch,
                     int   il) const;

    virtual ggml_tensor * build_rwkv_token_shift_load(
            ggml_context * ctx0,
             ggml_cgraph * gf,
             ggml_tensor * state_copy,
             ggml_tensor * state_mask,
      const llama_ubatch & ubatch,
                     int   il) const;

    virtual ggml_tensor * build_rwkv_token_shift_store(
            ggml_context * ctx0,
             ggml_tensor * token_shift,
      const llama_ubatch & ubatch,
                     int   il) const;

    virtual ggml_tensor * build_rwkv6_time_mix(
            ggml_context * ctx0,
             ggml_cgraph * gf,
             ggml_tensor * cur,
             ggml_tensor * x_prev,
             ggml_tensor * state_copy,
             ggml_tensor * state_mask,
      const llama_ubatch & ubatch,
                     int   il) const;
};
