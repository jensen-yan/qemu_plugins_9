#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <glib.h>
#include <qemu-plugin.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static GHashTable *block_counts;        // 记录基本块的执行次数， pc -> count
static GHashTable *block_instructions;  // 记录基本块的指令信息

static qemu_plugin_u64 total_insn_count;   // 总动态指令数

typedef struct {
    uint64_t pc;
    uint64_t count;
} BlockInfo;

typedef struct {
    uint64_t pc;
    char *instructions;
} BlockInstructions;

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    uint64_t tb_pc = (uint64_t)udata;
    uint64_t *count = g_hash_table_lookup(block_counts, GUINT_TO_POINTER(tb_pc));
    if (!count) {
        count = g_malloc(sizeof(*count));
        *count = 0;
        g_hash_table_insert(block_counts, GUINT_TO_POINTER(tb_pc), count);
    }
    (*count)++;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t n = qemu_plugin_tb_n_insns(tb);
    
    // 注册基本块执行回调
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         GUINT_TO_POINTER(pc));

    // 收集基本块的指令信息
    GString *instructions = g_string_new(NULL);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t insn_addr = qemu_plugin_insn_vaddr(insn);
        const char *disas = qemu_plugin_insn_disas(insn);
        // pc: disas 都输出到 instructions 字符串中
        g_string_append_printf(instructions, "  0x%" PRIx64 ": %s\n", insn_addr, disas);

        // 动态指令数++
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_ADD_U64, total_insn_count, 1);
    }
    
    BlockInstructions *block_instrs = g_malloc(sizeof(BlockInstructions));
    block_instrs->pc = pc;
    block_instrs->instructions = g_string_free(instructions, FALSE);
    g_hash_table_insert(block_instructions, GUINT_TO_POINTER(pc), block_instrs);
}

static gint compare_block_info(gconstpointer a, gconstpointer b)
{
    const BlockInfo *block_a = (const BlockInfo *)a;
    const BlockInfo *block_b = (const BlockInfo *)b;
    
    if (block_a->count > block_b->count)
        return -1;
    if (block_a->count < block_b->count)
        return 1;
    return 0;
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    GHashTableIter iter;
    gpointer key, value;
    GSList *block_list = NULL;

    g_hash_table_iter_init(&iter, block_counts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        BlockInfo *info = g_malloc(sizeof(BlockInfo));
        info->pc = GPOINTER_TO_UINT(key);
        info->count = *(uint64_t *)value;
        block_list = g_slist_prepend(block_list, info);
    }   // 把哈希表中的数据转移到链表中，方便排序

    block_list = g_slist_sort(block_list, compare_block_info);

    FILE *output_file = fopen("top_blocks_with_instructions.txt", "w");
    if (!output_file) {
        fprintf(stderr, "Failed to open output file\n");
        return;
    }
    
    // 输出总动态指令数
    fprintf(output_file, "Total Dynamic Instructions: %" PRIu64 "\n\n", qemu_plugin_u64_sum(total_insn_count));

    qemu_plugin_scoreboard_free(total_insn_count.score);

    fprintf(output_file, "Top 10 Most Executed Basic Blocks with Instructions:\n\n");
    GSList *l;
    int count = 0;
    for (l = block_list; l != NULL && count < 10; l = l->next, count++) {
        BlockInfo *info = (BlockInfo *)l->data;
        fprintf(output_file, "Basic Block at 0x%" PRIx64 " executed %" PRIu64 " times\n", 
                info->pc, info->count);
        
        BlockInstructions *block_instrs = g_hash_table_lookup(block_instructions, GUINT_TO_POINTER(info->pc));
        if (block_instrs) {
            fprintf(output_file, "Instructions:\n%s\n", block_instrs->instructions);
        }
        fprintf(output_file, "\n");
    }

    fclose(output_file);
    printf("Results have been written to top_blocks_with_instructions.txt\n");

    // 清理
    g_slist_free_full(block_list, g_free);
    g_hash_table_destroy(block_counts);
    g_hash_table_iter_init(&iter, block_instructions);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        BlockInstructions *block_instrs = (BlockInstructions *)value;
        g_free(block_instrs->instructions);
        g_free(block_instrs);
    }
    g_hash_table_destroy(block_instructions);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    block_counts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    block_instructions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    total_insn_count = qemu_plugin_scoreboard_u64(qemu_plugin_scoreboard_new(sizeof(uint64_t)));

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}