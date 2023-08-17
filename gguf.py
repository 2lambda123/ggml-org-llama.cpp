"""TODOs
1. Implement writers for known architectures, LLaMA in particular.
2. Add docstrings from the format specs.
3. After development is done, Convert it to a proper pip-installable Python package, and possibly move it to its own repo under ggml-org.
"""

import sys
import struct
import numpy as np

from enum import IntEnum
from typing import Any, IO, List

#
# constants
#

GGUF_MAGIC             = 0x47475546
GGUF_VERSION           = 1
GGUF_DEFAULT_ALIGNMENT = 32

# general
KEY_GENERAL_ARCHITECTURE         = "general.architecture"
KEY_GENERAL_QUANTIZATION_VERSION = "general.quantization_version"
KEY_GENERAL_ALIGNMENT            = "general.alignment"
KEY_GENERAL_NAME                 = "general.name"
KEY_GENERAL_AUTHOR               = "general.author"
KEY_GENERAL_URL                  = "general.url"
KEY_GENERAL_DESCRIPTION          = "general.description"
KEY_GENERAL_FILE_TYPE            = "general.file_type"
KEY_GENERAL_LICENSE              = "general.license"
KEY_GENERAL_SOURCE_URL           = "general.source.url"
KEY_GENERAL_SOURCE_HF_REPO       = "general.source.hugginface.repository"

# LLM
KEY_LLM_CONTEXT_LENGTH           = "{llm}.context_length"
KEY_LLM_EMBEDDING_LENGTH         = "{llm}.embedding_length"
KEY_LLM_BLOCK_COUNT              = "{llm}.block_count"
KEY_LLM_FEED_FORWARD_LENGTH      = "{llm}.feed_forward_length"
KEY_LLM_USE_PARALLEL_RESIDUAL    = "{llm}.use_parallel_residual"
KEY_LLM_TENSOR_DATA_LAYOUT       = "{llm}.tensor_data_layout"

# attention
KEY_ATTENTION_HEAD_COUNT         = "{llm}.attention.head_count"
KEY_ATTENTION_HEAD_COUNT_KV      = "{llm}.attention.head_count_kv"
KEY_ATTENTION_MAX_ALIBI_BIAS     = "{llm}.attention.max_alibi_bias"
KEY_ATTENTION_CLAMP_KQV          = "{llm}.attention.clamp_kqv"
KEY_ATTENTION_LAYERNORM_EPS      = "{llm}.attention.layer_norm_epsilon"
KEY_ATTENTION_LAYERNORM_RMS_EPS  = "{llm}.attention.layer_norm_rms_epsilon"

# RoPE
KEY_ROPE_DIMENSION_COUNT         = "{llm}.rope.dimension_count"
KEY_ROPE_SCALE                   = "{llm}.rope.scale"

# tokenization
KEY_TOKENIZER_MODEL      = "tokenizer.ggml.model"
KEY_TOKENIZER_LIST       = "tokenizer.ggml.tokens"
KEY_TOKENIZER_TOKEN_TYPE = "tokenizer.ggml.token_type"
KEY_TOKENIZER_SCORES     = "tokenizer.ggml.scores"
KEY_TOKENIZER_MERGES     = "tokenizer.ggml.merges"
KEY_TOKENIZER_BOS_ID     = "tokenizer.ggml.bos_token_id"
KEY_TOKENIZER_EOS_ID     = "tokenizer.ggml.eos_token_id"
KEY_TOKENIZER_UNK_ID     = "tokenizer.ggml.unknown_token_id"
KEY_TOKENIZER_SEP_ID     = "tokenizer.ggml.seperator_token_id"
KEY_TOKENIZER_PAD_ID     = "tokenizer.ggml.padding_token_id"
KEY_TOKENIZER_HF_JSON    = "tokenizer.huggingface.json"
KEY_TOKENIZER_RWKV       = "tokenizer.rwkv.world"

#
# recommended mapping of model tensor names for storage in gguf
#

def get_tensor_name_map(n_blocks : int):
    tensor_map = {}
    # Token embeddings
    mapped_to = "token_embd"
    tensor_map["gpt_neox.embed_in"]           = mapped_to # gptneox
    tensor_map["transformer.wte"]             = mapped_to # gpt2 mpt
    tensor_map["transformer.word_embeddings"] = mapped_to # falcon
    tensor_map["model.embed_tokens"]          = mapped_to # llama-hf
    tensor_map["tok_embeddings"]              = mapped_to # llama-pth
    # Position embeddings
    mapped_to = "pos_embd"
    tensor_map["transformer.wpe"] = mapped_to # gpt2
    # Output norm
    mapped_to = "output_norm"
    tensor_map["gpt_neox.final_layer_norm"] = mapped_to # gptneox
    tensor_map["transformer.ln_f"]          = mapped_to # gpt2 falcon
    tensor_map["transformer.norm_f"]        = mapped_to # mpt
    tensor_map["model.norm"]                = mapped_to # llama-hf
    tensor_map["norm"]                      = mapped_to # llama-pth
    # Output
    mapped_to = "output"
    tensor_map["embed_out"] = mapped_to # gptneox
    tensor_map["lm_head"]   = mapped_to # gpt2 mpt falcon llama-hf
    tensor_map["output"]    = mapped_to # llama-pth
    # Attention and fee-forward layer blocks
    for i in range(0,n_blocks):
        # Attention norm
        mapped_to = "blk."+str(i)+".attn_norm"
        tensor_map["gpt_neox.layers."+str(i)+".input_layernorm"] = mapped_to # gptneox
        tensor_map["transformer.h."+str(i)+".ln_1"]              = mapped_to # gpt2
        tensor_map["transformer.blocks."+str(i)+".norm_1"]       = mapped_to # mpt
        tensor_map["transformer.h."+str(i)+".input_layernorm"]   = mapped_to # falcon7b
        tensor_map["transformer.h."+str(i)+".ln_attn"]           = mapped_to # falcon40b
        tensor_map["model.layers."+str(i)+".input_layernorm"]    = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".attention_norm"]           = mapped_to # llama-pth
        # Attention norm 2
        mapped_to = "blk."+str(i)+".attn_norm_2"
        tensor_map["transformer.h."+str(i)+".ln_mlp"] = mapped_to # falcon40b
        # Attention query-key-value
        mapped_to = "blk."+str(i)+".attn_qkv"
        tensor_map["gpt_neox.layers."+str(i)+".attention.query_key_value"]    = mapped_to # gptneox
        tensor_map["transformer.h."+str(i)+".attn.c_attn"]                    = mapped_to # gpt2
        tensor_map["transformer.blocks."+str(i)+".attn.Wqkv"]                 = mapped_to # mpt
        tensor_map["transformer.h."+str(i)+".self_attention.query_key_value"] = mapped_to # falcon
        # Attention query
        mapped_to = "blk."+str(i)+".attn_q"
        tensor_map["model.layers."+str(i)+".self_attn.q_proj"] = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".attention.wq"]           = mapped_to # llama-pth
        # Attention key
        mapped_to = "blk."+str(i)+".attn_k"
        tensor_map["model.layers."+str(i)+".self_attn.k_proj"] = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".attention.wk"]           = mapped_to # llama-pth
        # Attention value
        mapped_to = "blk."+str(i)+".attn_v"
        tensor_map["model.layers."+str(i)+".self_attn.v_proj"] = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".attention.wv"]           = mapped_to # llama-pth
        # Attention output
        mapped_to = "blk."+str(i)+".attn_output"
        tensor_map["gpt_neox.layers."+str(i)+".attention.dense"]    = mapped_to # gptneox
        tensor_map["transformer.h."+str(i)+".attn.c_proj"]          = mapped_to # gpt2
        tensor_map["transformer.blocks."+str(i)+".attn.out_proj"]   = mapped_to # mpt
        tensor_map["transformer.h."+str(i)+".self_attention.dense"] = mapped_to # falcon
        tensor_map["model.layers."+str(i)+".self_attn.o_proj"]      = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".attention.wo"]                = mapped_to # llama-pth
        # Feed-forward norm
        mapped_to = "blk."+str(i)+".ffn_norm"
        tensor_map["gpt_neox.layers."+str(i)+".post_attention_layernorm"] = mapped_to # gptneox
        tensor_map["transformer.h."+str(i)+".ln_2"]                       = mapped_to # gpt2
        tensor_map["transformer.blocks."+str(i)+".norm_2"]                = mapped_to # mpt
        tensor_map["model.layers."+str(i)+".post_attention_layernorm"]    = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".ffn_norm"]                          = mapped_to # llama-pth
        # Feed-forward up
        mapped_to = "blk."+str(i)+".ffn_up"
        tensor_map["gpt_neox.layers."+str(i)+".mlp.dense_h_to_4h"] = mapped_to # gptneox
        tensor_map["transformer.h."+str(i)+".mlp.c_fc"]            = mapped_to # gpt2
        tensor_map["transformer.blocks."+str(i)+".ffn.up_proj"]    = mapped_to # mpt
        tensor_map["transformer.h."+str(i)+".mlp.dense_h_to_4h"]   = mapped_to # falcon
        tensor_map["model.layers."+str(i)+".mlp.up_proj"]          = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".feed_forward.w3"]            = mapped_to # llama-pth
        # Feed-forward gate
        mapped_to = "blk."+str(i)+".ffn_gate"
        tensor_map["model.layers."+str(i)+".mlp.gate_proj"] = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".feed_forward.w1"]     = mapped_to # llama-pth
        # Feed-forward down
        mapped_to = "blk."+str(i)+".ffn_down"
        tensor_map["gpt_neox.layers."+str(i)+".mlp.dense_4h_to_h"] = mapped_to # gptneox
        tensor_map["transformer.h."+str(i)+".mlp.c_proj"]          = mapped_to # gpt2
        tensor_map["transformer.blocks."+str(i)+".ffn.down_proj"]  = mapped_to # mpt
        tensor_map["transformer.h."+str(i)+".mlp.dense_4h_to_h"]   = mapped_to # falcon
        tensor_map["model.layers."+str(i)+".mlp.down_proj"]        = mapped_to # llama-hf
        tensor_map["layers."+str(i)+".feed_forward.w2"]            = mapped_to # llama-pth

    return tensor_map

#
# implementation
#

class GGMLQuantizationType(IntEnum):
    F32 = 0
    F16 = 1


class GGUFValueType(IntEnum):
    UINT8   = 0
    INT8    = 1
    UINT16  = 2
    INT16   = 3
    UINT32  = 4
    INT32   = 5
    FLOAT32 = 6
    BOOL    = 7
    STRING  = 8
    ARRAY   = 9

    @staticmethod
    def get_type(val):
        if isinstance(val, str) or isinstance(val, bytes) or isinstance(val, bytearray):
            return GGUFValueType.STRING
        elif isinstance(val, list):
            return GGUFValueType.ARRAY
        elif isinstance(val, float):
            return GGUFValueType.FLOAT32
        elif isinstance(val, bool):
            return GGUFValueType.BOOL
        elif isinstance(val, int):
            return GGUFValueType.INT32
        else:
            print("Unknown type: "+str(type(val)))
            sys.exit()


class GGUFWriter:
    def __init__(self, fout: IO):
        self.fout = fout
        self.offset_tensor = 0
        self.data_alignment = GGUF_DEFAULT_ALIGNMENT
        self.kv_data = b""
        self.kv_data_count = 0
        self.ti_data = b""
        self.ti_data_count = 0

    def write_header_to_file(self):
        self.fout.write(struct.pack("<I", GGUF_MAGIC))
        self.fout.write(struct.pack("<I", GGUF_VERSION))
        self.fout.write(struct.pack("<I", self.ti_data_count))
        self.fout.write(struct.pack("<I", self.kv_data_count))
        self.flush()
#        print("tensors " + str(self.ti_data_count) + " kv " + str(self.kv_data_count))

    def write_kv_data_to_file(self):
        self.fout.write(self.kv_data)
        self.flush()

    def write_ti_data_to_file(self):
        self.fout.write(self.ti_data)
        self.flush()

    @classmethod
    def open(cls, path: str) -> "GGUFWriter":
        f = open(path, "wb")
        return cls(f)

    def add_key(self, key: str):
        self.add_val(key, GGUFValueType.STRING, add_vtype=False)

    def add_uint8(self, key: str, val: int):
        self.add_key(key)
        self.add_val(val, GGUFValueType.UINT8)

    def add_int8(self, key: str, val: int):
        self.add_key(key)
        self.add_val(val, GGUFValueType.INT8)

    def add_uint16(self, key: str, val: int):
        self.add_key(key)
        self.add_val(val, GGUFValueType.UINT16)

    def add_int16(self, key: str, val: int):
        self.add_key(key)
        self.add_val(val, GGUFValueType.INT16)

    def add_uint32(self, key: str, val: int):
        self.add_key(key)
        self.add_val(val, GGUFValueType.UINT32)

    def add_int32(self, key: str, val: int):
        self.add_key(key)
        self.add_val(val, GGUFValueType.INT32)

    def add_float32(self, key: str, val: float):
        self.add_key(key)
        self.add_val(val, GGUFValueType.FLOAT32)

    def add_bool(self, key: str, val: bool):
        self.add_key(key)
        self.add_val(val, GGUFValueType.BOOL)

    def add_string(self, key: str, val: str):
        if len(val) == 0: return
        self.add_key(key)
        self.add_val(val, GGUFValueType.STRING)

    def add_array(self, key: str, val: list):
        if not isinstance(val, list):
            raise ValueError("Value must be a list for array type")

        self.add_key(key)
        self.add_val(val, GGUFValueType.ARRAY)

    def add_val(self: str, val: Any, vtype: GGUFValueType = None, add_vtype: bool = True):
        if vtype is None:
            vtype = GGUFValueType.get_type(val)

        if add_vtype:
            self.kv_data += struct.pack("<I", vtype)
            self.kv_data_count += 1

        if vtype == GGUFValueType.UINT8:
            self.kv_data += struct.pack("<B", val)
        elif vtype == GGUFValueType.INT8:
            self.kv_data += struct.pack("<b", val)
        elif vtype == GGUFValueType.UINT16:
            self.kv_data += struct.pack("<H", val)
        elif vtype == GGUFValueType.INT16:
            self.kv_data += struct.pack("<h", val)
        elif vtype == GGUFValueType.UINT32:
            self.kv_data += struct.pack("<I", val)
        elif vtype == GGUFValueType.INT32:
            self.kv_data += struct.pack("<i", val)
        elif vtype == GGUFValueType.FLOAT32:
            self.kv_data += struct.pack("<f", val)
        elif vtype == GGUFValueType.BOOL:
            self.kv_data += struct.pack("?", val)
        elif vtype == GGUFValueType.STRING:
            encoded_val = val.encode("utf8") if isinstance(val, str) else val
            self.kv_data += struct.pack("<I", len(encoded_val))
            self.kv_data += encoded_val
        elif vtype == GGUFValueType.ARRAY:
            ltype = set([GGUFValueType.get_type(item) for item in val])
            assert len(ltype) == 1, "All items in a GGUF array should be of the same type"
            self.kv_data += struct.pack("<I", list(ltype)[0])
            self.kv_data += struct.pack("<I", len(val))
            for item in val:
                self.add_val(item, add_vtype=False)
        else:
            raise ValueError("Invalid GGUF metadata value type")

    @staticmethod
    def ggml_pad(x: int, n: int) -> int:
        return ((x + n - 1) // n) * n

    def add_tensor_info(self, name: str, tensor_shape: np.ndarray, tensor_dtype: np.dtype, tensor_nbytes: int):
        encoded_name = name.encode("utf8")
        self.ti_data += struct.pack("<I", len(encoded_name))
        self.ti_data += encoded_name
        n_dims = len(tensor_shape)
        self.ti_data += struct.pack("<I", n_dims)
        for i in range(n_dims):
            self.ti_data += struct.pack("<I", tensor_shape[n_dims - 1 - i])

        assert tensor_dtype in (np.float32, np.float16), "Only F32 and F16 tensors are supported for now"
        dtype = GGMLQuantizationType.F32 if tensor_dtype == np.float32 else GGMLQuantizationType.F16
        self.ti_data += struct.pack("<I", dtype)
        self.ti_data += struct.pack("<Q", self.offset_tensor)
        self.offset_tensor += GGUFWriter.ggml_pad(tensor_nbytes, self.data_alignment)
        self.ti_data_count += 1

    def write_tensor_to_file(self, tensor: np.ndarray):
        pad = GGUFWriter.ggml_pad(self.fout.tell(), self.data_alignment) - self.fout.tell()
        if pad != 0:
            self.fout.write(bytes([0] * pad))

        tensor.tofile(self.fout)

        pad = GGUFWriter.ggml_pad(tensor.nbytes, self.data_alignment) - tensor.nbytes
        if pad != 0:
            self.fout.write(bytes([0] * pad))

    def flush(self):
        self.fout.flush()

    def close(self):
        self.fout.close()

    def add_architecture(self, architecture: str):
        self.add_string(KEY_GENERAL_ARCHITECTURE,
                        architecture)

    def add_author(self, author: str):
        self.add_string(KEY_GENERAL_AUTHOR, author)

    def add_tensor_data_layout(self, layout: str):
        self.add_string(KEY_LLM_TENSOR_DATA_LAYOUT , layout)

    def add_url(self, url: str):
        self.add_string(KEY_GENERAL_URL, url)

    def add_description(self, description: str):
        self.add_string(KEY_GENERAL_DESCRIPTION, description)

    def add_file_type(self, file_type: str):
        self.add_string(KEY_GENERAL_FILE_TYPE, file_type)

    def add_source_url(self, url: str):
        self.add_string(KEY_GENERAL_SOURCE_URL, url)

    def add_source_hf_repo(self, repo: str):
        self.add_string(KEY_GENERAL_SOURCE_HF_REPO, repo)

    def add_name(self, name: str):
        self.add_string(KEY_GENERAL_NAME, name)

    def add_quantization_version(self, quantization_version: GGMLQuantizationType):
        self.add_uint32(
            KEY_GENERAL_QUANTIZATION_VERSION, quantization_version)

    def add_custom_alignment(self, alignment: int):
        self.data_alignment = alignment
        self.add_uint32(KEY_GENERAL_ALIGNMENT, alignment)

    def add_context_length(self, llm: str, length: int):
        self.add_uint32(
            KEY_LLM_CONTEXT_LENGTH.format(llm=llm), length)

    def add_embedding_length(self, llm: str, length: int):
        self.add_uint32(
            KEY_LLM_EMBEDDING_LENGTH.format(llm=llm), length)

    def add_block_count(self, llm: str, length: int):
        self.add_uint32(
            KEY_LLM_BLOCK_COUNT.format(llm=llm), length)

    def add_feed_forward_length(self, llm: str, length: int):
        self.add_uint32(
            KEY_LLM_FEED_FORWARD_LENGTH.format(llm=llm), length)

    def add_parallel_residual(self, llm: str, use: bool):
        self.add_bool(
            KEY_LLM_USE_PARALLEL_RESIDUAL.format(llm=llm), use)

    def add_tensor_data_layout(self, llm: str, layout: str):
        self.add_string(
            KEY_LLM_TENSOR_DATA_LAYOUT.format(llm=llm), layout)

    def add_head_count(self, llm: str, count: int):
        self.add_uint32(
            KEY_ATTENTION_HEAD_COUNT.format(llm=llm), count)

    def add_head_count_kv(self, llm: str, count: int):
        self.add_uint32(
            KEY_ATTENTION_HEAD_COUNT_KV.format(llm=llm), count)

    def add_max_alibi_bias(self, llm: str, bias: float):
        self.add_float32(
            KEY_ATTENTION_MAX_ALIBI_BIAS.format(llm=llm), bias)

    def add_clamp_kqv(self, llm: str, value: float):
        self.add_float32(
            KEY_ATTENTION_CLAMP_KQV.format(llm=llm), value)

    def add_layer_norm_eps(self, llm: str, value: float):
        self.add_float32(
            KEY_ATTENTION_LAYERNORM_EPS.format(llm=llm), value)

    def add_layer_norm_rms_eps(self, llm: str, value: float):
        self.add_float32(
            KEY_ATTENTION_LAYERNORM_RMS_EPS.format(llm=llm), value)

    def add_rope_dimension_count(self, llm: str, count: int):
        self.add_uint32(
            KEY_ROPE_DIMENSION_COUNT.format(llm=llm), count)

    def add_rope_scale(self, llm: str, value:  float):
        self.add_float32(KEY_ROPE_SCALE.format(llm=llm), value)

    def add_tokenizer_model(self, model: str):
        self.add_string(KEY_TOKENIZER_MODEL, model)

    def add_token_list(self, tokens: List):
        self.add_array(KEY_TOKENIZER_LIST, tokens)

    def add_token_merges(self, merges: List):
        self.add_array(KEY_TOKENIZER_MERGES, merges)

    def add_token_types(self, types: List[int]):
        self.add_array(KEY_TOKENIZER_TOKEN_TYPE, types)

    def add_token_scores(self, scores: List[float]):
        self.add_array(KEY_TOKENIZER_SCORES, scores)

    def add_bos_token_id(self, id: int):
        self.add_uint32(KEY_TOKENIZER_BOS_ID, id)

    def add_eos_token_id(self, id: int):
        self.add_uint32(KEY_TOKENIZER_EOS_ID, id)

    def add_unk_token_id(self, id: int):
        self.add_uint32(KEY_TOKENIZER_UNK_ID, id)

    def add_sep_token_id(self, id: int):
        self.add_uint32(KEY_TOKENIZER_SEP_ID, id)

    def add_pad_token_id(self, id: int):
        self.add_uint32(KEY_TOKENIZER_PAD_ID, id)

# Example usage:
if __name__ == "__main__":
    # Example usage with a file
    gguf_writer = GGUFWriter.open("example.gguf")

    gguf_writer.add_architecture("llama")
    gguf_writer.add_uint32("answer", 42)  # Write a 32-bit integer
    gguf_writer.add_float32("answer_in_float", 42.0)  # Write a 32-bit float
    gguf_writer.add_custom_alignment(64)
    tensor1 = np.ones((32,), dtype=np.float32) * 100.0
    tensor2 = np.ones((32,), dtype=np.float32) * 101.0
    gguf_writer.add_tensor_info("tensor0", tensor1)
    gguf_writer.add_tensor_info("tensor1", tensor2)

    gguf_writer.write_header_to_file()
    gguf_writer.write_kv_data_to_file()
    gguf_writer.write_ti_data_to_file()
    gguf_writer.write_tensor_to_file(tensor1)
    gguf_writer.write_tensor_to_file(tensor2)

    gguf_writer.close()
