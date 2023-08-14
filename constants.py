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
KEY_TOKENIZER_MODEL   = "tokenizer.ggml.model"
KEY_TOKENIZER_LIST    = "tokenizer.ggml.tokens"
KEY_TOKENIZER_SCORES  = "tokenizer.ggml.scores"
KEY_TOKENIZER_MERGES  = "tokenizer.ggml.merges"
KEY_TOKENIZER_BOS_ID  = "tokenizer.ggml.bos_token_id"
KEY_TOKENIZER_EOS_ID  = "tokenizer.ggml.eos_token_id"
KEY_TOKENIZER_UNK_ID  = "tokenizer.ggml.unknown_token_id"
KEY_TOKENIZER_SEP_ID  = "tokenizer.ggml.seperator_token_id"
KEY_TOKENIZER_PAD_ID  = "tokenizer.ggml.padding_token_id"
KEY_TOKENIZER_HF_JSON = "tokenizer.huggingface.json"
KEY_TOKENIZER_RWKV    = "tokenizer.rwkv.world"
KEY_TOKENIZER_BOS_ID  = "tokenizer.ggml.bos_token_id"
KEY_TOKENIZER_EOS_ID  = "tokenizer.ggml.eos_token_id"
KEY_TOKENIZER_UNK_ID  = "tokenizer.ggml.unknown_token_id"
KEY_TOKENIZER_SEP_ID  = "tokenizer.ggml.separator_token_id"
KEY_TOKENIZER_PAD_ID  = "tokenizer.ggml.padding_token_id"
