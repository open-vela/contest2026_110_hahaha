/*********************************************************
 * @file lv_offline_nav.c
 * @author ^^^^^^^ ()
 * @brief 实现可移植的离线路网路径规划接口
 * @version 1.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *
 * @note ChangeLog:
 *
 *********************************************************/
#include "lv_offline_nav.h"

#if defined(__has_include)
#  if __has_include(<lvgl/lvgl.h>)
#    include <lvgl/lvgl.h>
#  else
#    include "lvgl.h"
#  endif
#else
#  include "lvgl.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LV_OFFLINE_NAV_DEFAULT_GRAPH_PATH "L:/sdcard/nav/road_graph.bin" /* 默认离线路网文件路径，调用者可通过配置覆盖 */
#define LV_OFFLINE_NAV_MAGIC "EBNAV001" /* 路网文件格式魔数，必须与转换脚本输出保持一致 */
#define LV_OFFLINE_NAV_MAGIC_SIZE 8U /* 路网文件魔数字节数 */
#define LV_OFFLINE_NAV_INVALID_NODE 0xFFFFFFFFu /* 无效路网节点索引哨兵值 */
#define LV_OFFLINE_NAV_DISTANCE_INF 0xFFFFFFFFu /* Dijkstra 距离数组中的不可达距离值 */
#define LV_OFFLINE_NAV_FLAG_BICYCLE_DESIGNATED 0x0002u /* 路网边标志位，表示 OSM 标注 bicycle=yes/designated 等自行车可通行属性 */
#define LV_OFFLINE_NAV_FLAG_CYCLEWAY 0x0004u /* 路网边标志位，表示 OSM 标注 highway=cycleway 或 cycleway=lane/track 等自行车道属性 */
#define LV_OFFLINE_NAV_FLAG_BICYCLE_FORBIDDEN 0x0008u /* 路网边标志位，表示 OSM 标注 bicycle=no 或 access=no 等自行车禁行属性 */
#define LV_OFFLINE_NAV_BIKE_COST_SCALE 100u /* 自行车路径权重缩放基准，100 表示使用道路真实长度作为规划代价 */
#define LV_OFFLINE_NAV_ROAD_CLASS_MOTORWAY 1u /* OSM motorway 道路等级编码，自行车规划默认禁用 */
#define LV_OFFLINE_NAV_ROAD_CLASS_TRUNK 2u /* OSM trunk 道路等级编码，自行车规划默认强惩罚 */
#define LV_OFFLINE_NAV_ROAD_CLASS_PRIMARY 3u /* OSM primary 道路等级编码，自行车规划默认较强惩罚 */
#define LV_OFFLINE_NAV_ROAD_CLASS_SECONDARY 4u /* OSM secondary 道路等级编码，自行车规划默认中等惩罚 */
#define LV_OFFLINE_NAV_ROAD_CLASS_TERTIARY 5u /* OSM tertiary 道路等级编码，自行车规划默认轻惩罚 */
#define LV_OFFLINE_NAV_ROAD_CLASS_UNCLASSIFIED 6u /* OSM unclassified 道路等级编码，自行车规划按普通道路处理 */
#define LV_OFFLINE_NAV_ROAD_CLASS_RESIDENTIAL 7u /* OSM residential 道路等级编码，自行车规划优先于主干道 */
#define LV_OFFLINE_NAV_ROAD_CLASS_SERVICE 8u /* OSM service 道路等级编码，自行车规划按低速支路处理 */
#define LV_OFFLINE_NAV_ROAD_CLASS_LIVING_STREET 9u /* OSM living_street 道路等级编码，自行车规划优先选择 */
#define LV_OFFLINE_NAV_ROAD_CLASS_CYCLEWAY 10u /* OSM cycleway 道路等级编码，自行车规划最高优先级 */
#define LV_OFFLINE_NAV_ROAD_CLASS_FOOTWAY 11u /* OSM footway 道路等级编码，无自行车标记时默认惩罚 */
#define LV_OFFLINE_NAV_ROAD_CLASS_PATH 12u /* OSM path 道路等级编码，自行车规划按非机动车路径处理 */
#define LV_OFFLINE_NAV_ROAD_CLASS_TRACK 13u /* OSM track 道路等级编码，自行车规划按可通行土路处理 */
#define LV_OFFLINE_NAV_ROAD_CLASS_PEDESTRIAN 14u /* OSM pedestrian 道路等级编码，无自行车标记时默认惩罚 */
#define LV_OFFLINE_NAV_ROAD_CLASS_CONSTRUCTION 15u /* OSM construction 道路等级编码，自行车规划默认禁用 */
#define LV_OFFLINE_NAV_DEFAULT_TURN_MIN_DEGREE 35.0 /* 默认小于该角度的道路方向变化不提示转向 */
#define LV_OFFLINE_NAV_DEFAULT_UTURN_MIN_DEGREE 145.0 /* 默认大于等于该角度的道路方向变化提示掉头 */
#define LV_OFFLINE_NAV_PI 3.14159265358979323846 /* 转向角度和地表距离计算使用的圆周率 */
#define LV_OFFLINE_NAV_EARTH_RADIUS_M 6378137.0 /* 路线里程近似计算使用的地球半径，单位为米 */
#define LV_OFFLINE_NAV_LON_LAT_SCALE 10000000.0 /* E7 经纬度转换成十进制度的比例 */

typedef struct {
    uint32_t node_count; /* 文件内节点数量，必须不超过配置允许的最大节点数量 */
    uint32_t edge_count; /* 文件内有向边数量，必须不超过配置允许的最大边数量 */
    int32_t min_lon_e7; /* 路网覆盖范围的最小经度 E7 定点值，用于格式校验和调试 */
    int32_t min_lat_e7; /* 路网覆盖范围的最小纬度 E7 定点值，用于格式校验和调试 */
    int32_t max_lon_e7; /* 路网覆盖范围的最大经度 E7 定点值，用于格式校验和调试 */
    int32_t max_lat_e7; /* 路网覆盖范围的最大纬度 E7 定点值，用于格式校验和调试 */
    uint32_t reserved; /* 预留字段，当前版本读取后不参与路径规划 */
} lv_offline_nav_graph_header_t;

typedef struct {
    int32_t lon_e7; /* 路网节点经度 E7 定点值，用于路线输出和距离计算 */
    int32_t lat_e7; /* 路网节点纬度 E7 定点值，用于路线输出和距离计算 */
} lv_offline_nav_node_t;

typedef struct {
    uint32_t from; /* 有向边起点节点索引，必须小于当前节点数量 */
    uint32_t to; /* 有向边终点节点索引，必须小于当前节点数量 */
    uint32_t length_mm; /* 有向边道路长度，单位为毫米，用作 Dijkstra 基础权重 */
    uint16_t flags; /* 道路标志位，用于自行车通行偏好和禁行判断 */
    uint16_t road_class; /* OSM 道路等级编码，用于自行车规划代价计算 */
} lv_offline_nav_edge_t;

typedef struct {
    lv_offline_nav_config_t config; /* 本次规划使用的归一化配置副本 */
    lv_offline_nav_graph_header_t header; /* 当前规划使用的路网文件头，保存节点边数量和覆盖范围 */
    lv_offline_nav_node_t *nodes; /* 规划期间加载的节点数组，所有权属于上下文并在规划结束释放 */
    lv_offline_nav_edge_t *edges; /* 规划期间加载的有向边数组，所有权属于上下文并在规划结束释放 */
    int32_t *edge_head; /* 邻接表头索引数组，-1 表示该节点没有出边 */
    int32_t *edge_next; /* 邻接表下一条边索引数组，-1 表示链表结束 */
    uint32_t *distance; /* Dijkstra 起点到各节点的自行车规划代价数组，数值越小越优先 */
    int32_t *previous_node; /* Dijkstra 最短路前驱节点数组，-1 表示尚无前驱 */
    bool *visited; /* Dijkstra 节点访问标记数组，true 表示最短距离已确定 */
    uint32_t *route_nodes; /* 当前规划路线节点序列数组，按起点到终点排列 */
    uint32_t route_point_count; /* 当前规划路线中的有效节点数量 */
    uint32_t route_distance_mm; /* 当前规划路线的加权总代价，单位为毫米权重 */
} lv_offline_nav_context_t;

/*********************************************************
 * @brief 修正调用者传入的离线导航配置
 * @param config 待修正配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_nav_normalize_config(lv_offline_nav_config_t *config)
{
    if(config == NULL) {
        return;
    }

    if(config->graph_path == NULL) {
        config->graph_path = LV_OFFLINE_NAV_DEFAULT_GRAPH_PATH;
    }
    if(config->max_nodes == 0U) {
        config->max_nodes = LV_OFFLINE_NAV_DEFAULT_MAX_NODES;
    }
    if(config->max_edges == 0U) {
        config->max_edges = LV_OFFLINE_NAV_DEFAULT_MAX_EDGES;
    }
    if(config->max_route_points == 0U) {
        config->max_route_points = LV_OFFLINE_NAV_DEFAULT_MAX_ROUTE_POINTS;
    }
    if(config->max_render_points == 0U) {
        config->max_render_points = LV_OFFLINE_NAV_DEFAULT_MAX_RENDER_POINTS;
    }
    if(config->max_turn_events == 0U) {
        config->max_turn_events = LV_OFFLINE_NAV_DEFAULT_MAX_TURN_EVENTS;
    }
    if(config->turn_min_degree <= 0.0) {
        config->turn_min_degree = LV_OFFLINE_NAV_DEFAULT_TURN_MIN_DEGREE;
    }
    if(config->uturn_min_degree <= config->turn_min_degree) {
        config->uturn_min_degree = LV_OFFLINE_NAV_DEFAULT_UTURN_MIN_DEGREE;
    }
    if(config->malloc_cb == NULL || config->free_cb == NULL) {
        config->malloc_cb = NULL;
        config->free_cb = NULL;
        config->mem_user_data = NULL;
    }
}

/*********************************************************
 * @brief 申请导航规划临时缓存
 * @param ctx 导航规划上下文指针，不能为空
 * @param size 需要申请的字节数，必须大于 0
 * @return void* 申请成功的缓存指针，失败时返回 NULL
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void *lv_offline_nav_alloc_memory(lv_offline_nav_context_t *ctx, size_t size)
{
    void *buffer; /* 申请到的导航临时缓存，生命周期由规划上下文管理 */

    if(ctx == NULL || size == 0U) {
        return NULL;
    }

    if(ctx->config.malloc_cb != NULL) {
        buffer = ctx->config.malloc_cb(size, ctx->config.mem_user_data);
    } else {
        buffer = lv_malloc(size);
    }

    return buffer;
}

/*********************************************************
 * @brief 释放导航规划临时缓存
 * @param ctx 导航规划上下文指针，不能为空
 * @param buffer 需要释放的缓存指针，允许为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_nav_free_memory(lv_offline_nav_context_t *ctx, void *buffer)
{
    if(ctx == NULL || buffer == NULL) {
        return;
    }

    if(ctx->config.free_cb != NULL) {
        ctx->config.free_cb(buffer, ctx->config.mem_user_data);
    } else {
        lv_free(buffer);
    }
}

/*********************************************************
 * @brief 释放导航规划上下文内的所有动态缓存
 * @param ctx 导航规划上下文指针，允许为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_nav_free_context(lv_offline_nav_context_t *ctx)
{
    lv_offline_nav_config_t config; /* 释放过程保留的配置副本，避免清零后丢失内存回调 */

    if(ctx == NULL) {
        return;
    }

    config = ctx->config;
    lv_offline_nav_free_memory(ctx, ctx->nodes);
    lv_offline_nav_free_memory(ctx, ctx->edges);
    lv_offline_nav_free_memory(ctx, ctx->edge_head);
    lv_offline_nav_free_memory(ctx, ctx->edge_next);
    lv_offline_nav_free_memory(ctx, ctx->distance);
    lv_offline_nav_free_memory(ctx, ctx->previous_node);
    lv_offline_nav_free_memory(ctx, ctx->visited);
    lv_offline_nav_free_memory(ctx, ctx->route_nodes);
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = config;
}

/*********************************************************
 * @brief 根据文件头申请路网和路径规划缓存
 * @param ctx 导航规划上下文指针，不能为空，header 必须已填充
 * @return bool true 表示缓存申请成功，false 表示内存不足
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_alloc_context_buffers(lv_offline_nav_context_t *ctx)
{
    size_t node_size; /* 节点数组申请大小，单位为字节 */
    size_t edge_size; /* 有向边数组申请大小，单位为字节 */
    size_t edge_head_size; /* 邻接表头数组申请大小，单位为字节 */
    size_t edge_next_size; /* 邻接表下一边数组申请大小，单位为字节 */
    size_t distance_size; /* Dijkstra 距离数组申请大小，单位为字节 */
    size_t previous_size; /* Dijkstra 前驱数组申请大小，单位为字节 */
    size_t visited_size; /* Dijkstra 访问标记数组申请大小，单位为字节 */
    size_t route_size; /* 路线节点数组申请大小，单位为字节 */

    if(ctx == NULL) {
        return false;
    }

    node_size = (size_t)ctx->header.node_count * sizeof(ctx->nodes[0]);
    edge_size = (size_t)ctx->header.edge_count * sizeof(ctx->edges[0]);
    edge_head_size = (size_t)ctx->header.node_count * sizeof(ctx->edge_head[0]);
    edge_next_size = (size_t)ctx->header.edge_count * sizeof(ctx->edge_next[0]);
    distance_size = (size_t)ctx->header.node_count * sizeof(ctx->distance[0]);
    previous_size = (size_t)ctx->header.node_count * sizeof(ctx->previous_node[0]);
    visited_size = (size_t)ctx->header.node_count * sizeof(ctx->visited[0]);
    route_size = (size_t)ctx->config.max_route_points * sizeof(ctx->route_nodes[0]);

    ctx->nodes = (lv_offline_nav_node_t *)lv_offline_nav_alloc_memory(ctx, node_size);
    ctx->edges = (lv_offline_nav_edge_t *)lv_offline_nav_alloc_memory(ctx, edge_size);
    ctx->edge_head = (int32_t *)lv_offline_nav_alloc_memory(ctx, edge_head_size);
    ctx->edge_next = (int32_t *)lv_offline_nav_alloc_memory(ctx, edge_next_size);
    ctx->distance = (uint32_t *)lv_offline_nav_alloc_memory(ctx, distance_size);
    ctx->previous_node = (int32_t *)lv_offline_nav_alloc_memory(ctx, previous_size);
    ctx->visited = (bool *)lv_offline_nav_alloc_memory(ctx, visited_size);
    ctx->route_nodes = (uint32_t *)lv_offline_nav_alloc_memory(ctx, route_size);

    if(ctx->nodes == NULL || ctx->edges == NULL || ctx->edge_head == NULL || ctx->edge_next == NULL ||
       ctx->distance == NULL || ctx->previous_node == NULL || ctx->visited == NULL || ctx->route_nodes == NULL) {
        lv_offline_nav_free_context(ctx);
        return false;
    }

    return true;
}

/*********************************************************
 * @brief 校验路网文件头是否在当前配置可处理范围内
 * @param ctx 导航规划上下文指针，不能为空
 * @return bool true 表示文件头合法，false 表示节点或边数量不受支持
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_validate_header(const lv_offline_nav_context_t *ctx)
{
    if(ctx == NULL) {
        return false;
    }

    if(ctx->header.node_count == 0U || ctx->header.edge_count == 0U) {
        return false;
    }

    if(ctx->header.node_count > ctx->config.max_nodes || ctx->header.edge_count > ctx->config.max_edges) {
        return false;
    }

    return true;
}

/*********************************************************
 * @brief 从 LVGL 文件系统读取指定长度的数据
 * @param file 文件句柄指针，不能为空
 * @param buffer 输出缓冲区指针，不能为空
 * @param size 期望读取的字节数
 * @return bool true 表示完整读取，false 表示文件错误或长度不足
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_read_exact(lv_fs_file_t *file, void *buffer, uint32_t size)
{
    uint32_t read_size; /* LVGL 文件系统本次实际读取的字节数 */

    if(file == NULL || buffer == NULL) {
        return false;
    }

    read_size = 0U;
    if(lv_fs_read(file, buffer, size, &read_size) != LV_FS_RES_OK) {
        return false;
    }

    return read_size == size;
}

/*********************************************************
 * @brief 根据已加载边表构建邻接链表
 * @param ctx 导航规划上下文指针，不能为空
 * @return bool true 表示邻接表构建成功，false 表示边表存在越界节点索引
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_build_adjacency(lv_offline_nav_context_t *ctx)
{
    uint32_t node_index; /* 当前初始化的节点索引 */
    uint32_t edge_index; /* 当前挂接到邻接表的有向边索引 */
    lv_offline_nav_edge_t *edge; /* 当前检查的有向边指针，指向上下文边缓存 */

    if(ctx == NULL) {
        return false;
    }

    for(node_index = 0U; node_index < ctx->header.node_count; node_index++) {
        ctx->edge_head[node_index] = -1;
    }

    for(edge_index = 0U; edge_index < ctx->header.edge_count; edge_index++) {
        edge = &ctx->edges[edge_index];
        if(edge->from >= ctx->header.node_count || edge->to >= ctx->header.node_count) {
            return false;
        }

        ctx->edge_next[edge_index] = ctx->edge_head[edge->from];
        ctx->edge_head[edge->from] = (int32_t)edge_index;
    }

    return true;
}

/*********************************************************
 * @brief 加载设备端二进制路网文件到一次性规划上下文
 * @param ctx 导航规划上下文指针，不能为空，调用前必须已清零
 * @return bool true 表示路网已加载并可用于规划，false 表示文件不存在或格式不匹配
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_load_graph(lv_offline_nav_context_t *ctx)
{
    lv_fs_file_t file; /* LVGL 文件系统句柄，用于读取路网文件 */
    char magic[LV_OFFLINE_NAV_MAGIC_SIZE]; /* 文件魔数缓冲区，读取后与 LV_OFFLINE_NAV_MAGIC 对比 */
    bool loaded; /* 本次加载流程结果，true 表示所有读取和校验均成功 */

    if(ctx == NULL || ctx->config.graph_path == NULL) {
        return false;
    }

    loaded = false;
    if(lv_fs_open(&file, ctx->config.graph_path, LV_FS_MODE_RD) != LV_FS_RES_OK) {
        printf("[nav-debug] load_graph: lv_fs_open failed path=%s\n",
              ctx->config.graph_path);
        return false;
    }

    /* 按固定二进制格式顺序读取，避免在设备端解析体积较大的 OSM PBF。 */
    if(!lv_offline_nav_read_exact(&file, magic, sizeof(magic))) {
        printf("[nav-debug] load_graph: failed reading magic\n");
    } else if(memcmp(magic, LV_OFFLINE_NAV_MAGIC, LV_OFFLINE_NAV_MAGIC_SIZE) != 0) {
        printf("[nav-debug] load_graph: magic mismatch\n");
    } else if(!lv_offline_nav_read_exact(&file, &ctx->header, sizeof(ctx->header))) {
        printf("[nav-debug] load_graph: failed reading header\n");
    } else if(!lv_offline_nav_validate_header(ctx)) {
        printf("[nav-debug] load_graph: header invalid "
              "node_count=%lu edge_count=%lu\n",
              (unsigned long)ctx->header.node_count,
              (unsigned long)ctx->header.edge_count);
    } else if(!lv_offline_nav_alloc_context_buffers(ctx)) {
        printf("[nav-debug] load_graph: buffer alloc failed\n");
    } else if(!lv_offline_nav_read_exact(&file, ctx->nodes,
              ctx->header.node_count * (uint32_t)sizeof(ctx->nodes[0]))) {
        printf("[nav-debug] load_graph: failed reading nodes\n");
    } else if(!lv_offline_nav_read_exact(&file, ctx->edges,
              ctx->header.edge_count * (uint32_t)sizeof(ctx->edges[0]))) {
        printf("[nav-debug] load_graph: failed reading edges\n");
    } else if(!lv_offline_nav_build_adjacency(ctx)) {
        printf("[nav-debug] load_graph: build_adjacency failed\n");
    } else {
        printf("[nav-debug] load_graph: OK node_count=%lu edge_count=%lu\n",
              (unsigned long)ctx->header.node_count,
              (unsigned long)ctx->header.edge_count);
        loaded = true;
    }

    lv_fs_close(&file);
    if(!loaded) {
        lv_offline_nav_free_context(ctx);
    }

    return loaded;
}

/*********************************************************
 * @brief 查找距离指定经纬度最近的路网节点
 * @param ctx 导航规划上下文指针，不能为空
 * @param lon_e7 目标经度 E7 定点值，单位为 1e-7 度
 * @param lat_e7 目标纬度 E7 定点值，单位为 1e-7 度
 * @return uint32_t 最近节点索引，未加载节点时返回 LV_OFFLINE_NAV_INVALID_NODE
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static uint32_t lv_offline_nav_find_nearest_node(lv_offline_nav_context_t *ctx, int32_t lon_e7, int32_t lat_e7)
{
    uint32_t node_index; /* 当前参与距离比较的节点索引 */
    uint32_t nearest_node; /* 当前找到的最近节点索引 */
    uint64_t nearest_distance; /* 当前最小经纬度平方距离，用于避免开方计算 */
    uint64_t node_distance; /* 当前节点到目标点的经纬度平方距离 */
    int64_t delta_lon; /* 当前节点与目标点的经度差，使用 64 位避免平方溢出 */
    int64_t delta_lat; /* 当前节点与目标点的纬度差，使用 64 位避免平方溢出 */

    if(ctx == NULL || ctx->header.node_count == 0U || ctx->nodes == NULL) {
        return LV_OFFLINE_NAV_INVALID_NODE;
    }

    nearest_node = LV_OFFLINE_NAV_INVALID_NODE;
    nearest_distance = UINT64_MAX;

    for(node_index = 0U; node_index < ctx->header.node_count; node_index++) {
        delta_lon = (int64_t)ctx->nodes[node_index].lon_e7 - lon_e7;
        delta_lat = (int64_t)ctx->nodes[node_index].lat_e7 - lat_e7;
        node_distance = (uint64_t)(delta_lon * delta_lon + delta_lat * delta_lat);

        if(node_distance < nearest_distance) {
            nearest_distance = node_distance;
            nearest_node = node_index;
        }
    }

    return nearest_node;
}

/*********************************************************
 * @brief 初始化 Dijkstra 运行状态
 * @param ctx 导航规划上下文指针，不能为空
 * @param start_node 起点路网节点索引
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_nav_init_dijkstra(lv_offline_nav_context_t *ctx, uint32_t start_node)
{
    uint32_t node_index; /* 当前初始化的节点索引 */

    if(ctx == NULL) {
        return;
    }

    for(node_index = 0U; node_index < ctx->header.node_count; node_index++) {
        ctx->distance[node_index] = LV_OFFLINE_NAV_DISTANCE_INF;
        ctx->previous_node[node_index] = -1;
        ctx->visited[node_index] = false;
    }

    ctx->distance[start_node] = 0U;
}

/*********************************************************
 * @brief 从未访问节点中选出当前距离最短的节点
 * @param ctx 导航规划上下文指针，不能为空
 * @return uint32_t 最短候选节点索引，不存在可达候选时返回 LV_OFFLINE_NAV_INVALID_NODE
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static uint32_t lv_offline_nav_pick_closest_unvisited(lv_offline_nav_context_t *ctx)
{
    uint32_t node_index; /* 当前扫描的节点索引 */
    uint32_t closest_node; /* 当前距离最短的未访问节点索引 */
    uint32_t closest_distance; /* 当前距离最短的候选距离，单位为毫米权重 */

    if(ctx == NULL) {
        return LV_OFFLINE_NAV_INVALID_NODE;
    }

    closest_node = LV_OFFLINE_NAV_INVALID_NODE;
    closest_distance = LV_OFFLINE_NAV_DISTANCE_INF;

    for(node_index = 0U; node_index < ctx->header.node_count; node_index++) {
        if(!ctx->visited[node_index] && ctx->distance[node_index] < closest_distance) {
            closest_distance = ctx->distance[node_index];
            closest_node = node_index;
        }
    }

    return closest_node;
}

/*********************************************************
 * @brief 根据道路属性计算自行车导航规划代价
 * @param edge 路网有向边指针，不能为空
 * @return uint32_t 自行车规划代价，返回 LV_OFFLINE_NAV_DISTANCE_INF 表示该边不适合自行车通行
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static uint32_t lv_offline_nav_get_bike_edge_cost(const lv_offline_nav_edge_t *edge)
{
    uint32_t scale; /* 当前道路类型对应的自行车权重缩放值，越小越优先 */
    uint64_t cost; /* 使用 64 位保存中间代价，避免长度乘权重时溢出 */

    if(edge == NULL || edge->length_mm == 0U) {
        return LV_OFFLINE_NAV_DISTANCE_INF;
    }

    if((edge->flags & LV_OFFLINE_NAV_FLAG_BICYCLE_FORBIDDEN) != 0U ||
       edge->road_class == LV_OFFLINE_NAV_ROAD_CLASS_MOTORWAY ||
       edge->road_class == LV_OFFLINE_NAV_ROAD_CLASS_CONSTRUCTION) {
        return LV_OFFLINE_NAV_DISTANCE_INF;
    }

    switch(edge->road_class) {
    case LV_OFFLINE_NAV_ROAD_CLASS_CYCLEWAY:
        scale = 35U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_PATH:
        scale = 65U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_LIVING_STREET:
        scale = 75U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_RESIDENTIAL:
        scale = 90U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_SERVICE:
    case LV_OFFLINE_NAV_ROAD_CLASS_TRACK:
        scale = 100U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_FOOTWAY:
    case LV_OFFLINE_NAV_ROAD_CLASS_PEDESTRIAN:
        scale = 160U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_UNCLASSIFIED:
        scale = 115U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_TERTIARY:
        scale = 140U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_SECONDARY:
        scale = 190U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_PRIMARY:
        scale = 260U;
        break;
    case LV_OFFLINE_NAV_ROAD_CLASS_TRUNK:
        scale = 360U;
        break;
    default:
        scale = LV_OFFLINE_NAV_BIKE_COST_SCALE;
        break;
    }

    if((edge->flags & LV_OFFLINE_NAV_FLAG_CYCLEWAY) != 0U) {
        scale = 35U;
    } else if((edge->flags & LV_OFFLINE_NAV_FLAG_BICYCLE_DESIGNATED) != 0U && scale > 55U) {
        scale = 55U;
    }

    cost = ((uint64_t)edge->length_mm * scale + LV_OFFLINE_NAV_BIKE_COST_SCALE - 1U) /
           LV_OFFLINE_NAV_BIKE_COST_SCALE;
    if(cost == 0U) {
        cost = 1U;
    }

    if(cost >= LV_OFFLINE_NAV_DISTANCE_INF) {
        return LV_OFFLINE_NAV_DISTANCE_INF;
    }

    return (uint32_t)cost;
}

/*********************************************************
 * @brief 松弛指定节点的所有出边
 * @param ctx 导航规划上下文指针，不能为空
 * @param node 当前已确定最短距离的路网节点索引
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_nav_relax_outgoing_edges(lv_offline_nav_context_t *ctx, uint32_t node)
{
    int32_t edge_index; /* 当前遍历的邻接边索引，-1 表示出边链表结束 */
    lv_offline_nav_edge_t *edge; /* 当前松弛的有向边指针，指向上下文边缓存 */
    uint32_t edge_cost; /* 当前边的自行车规划代价，禁行边使用无穷大跳过 */
    uint32_t candidate_distance; /* 经当前边到达终点节点的候选自行车规划代价 */

    if(ctx == NULL) {
        return;
    }

    edge_index = ctx->edge_head[node];
    while(edge_index >= 0) {
        edge = &ctx->edges[edge_index];
        edge_cost = lv_offline_nav_get_bike_edge_cost(edge);

        if(edge_cost != LV_OFFLINE_NAV_DISTANCE_INF && ctx->distance[node] <= LV_OFFLINE_NAV_DISTANCE_INF - edge_cost) {
            candidate_distance = ctx->distance[node] + edge_cost;
            if(candidate_distance < ctx->distance[edge->to]) {
                ctx->distance[edge->to] = candidate_distance;
                ctx->previous_node[edge->to] = (int32_t)node;
            }
        }

        edge_index = ctx->edge_next[edge_index];
    }
}

/*********************************************************
 * @brief 根据前驱数组重建起点到终点的路线节点序列
 * @param ctx 导航规划上下文指针，不能为空
 * @param start_node 起点路网节点索引
 * @param end_node 终点路网节点索引
 * @return bool true 表示路线序列已生成，false 表示路线不可达或超出缓存容量
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_build_route(lv_offline_nav_context_t *ctx, uint32_t start_node, uint32_t end_node)
{
    uint32_t cursor; /* 当前回溯的路网节点索引 */
    uint32_t reverse_count; /* 反向回溯阶段已写入的节点数量 */
    uint32_t index; /* 路线反转时使用的数组索引 */
    uint32_t temp_node; /* 路线反转时临时保存的节点索引 */

    if(ctx == NULL) {
        return false;
    }

    cursor = end_node;
    reverse_count = 0U;

    while(cursor != LV_OFFLINE_NAV_INVALID_NODE) {
        if(reverse_count >= ctx->config.max_route_points) {
            return false;
        }

        ctx->route_nodes[reverse_count] = cursor;
        reverse_count++;

        if(cursor == start_node) {
            break;
        }

        if(ctx->previous_node[cursor] < 0) {
            return false;
        }

        cursor = (uint32_t)ctx->previous_node[cursor];
    }

    if(reverse_count == 0U || ctx->route_nodes[reverse_count - 1U] != start_node) {
        return false;
    }

    /* Dijkstra 前驱链从终点回到起点，需要反转后才能按行驶方向输出。 */
    for(index = 0U; index < reverse_count / 2U; index++) {
        temp_node = ctx->route_nodes[index];
        ctx->route_nodes[index] = ctx->route_nodes[reverse_count - 1U - index];
        ctx->route_nodes[reverse_count - 1U - index] = temp_node;
    }

    ctx->route_point_count = reverse_count;
    ctx->route_distance_mm = ctx->distance[end_node];

    return true;
}

/*********************************************************
 * @brief 在已加载路网上按节点索引执行 Dijkstra 路径规划
 * @param ctx 导航规划上下文指针，不能为空
 * @param start_node 起点路网节点索引
 * @param end_node 终点路网节点索引
 * @return bool true 表示规划成功，false 表示节点无效或终点不可达
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_plan_route_by_node(lv_offline_nav_context_t *ctx, uint32_t start_node, uint32_t end_node)
{
    uint32_t step_index; /* Dijkstra 主循环步数，最多扫描全部节点 */
    uint32_t current_node; /* 当前距离最短且未访问的节点索引 */
    bool route_ready; /* 路线节点序列重建结果，true 表示可进入输出阶段 */

    if(ctx == NULL || start_node >= ctx->header.node_count || end_node >= ctx->header.node_count) {
        return false;
    }

    lv_offline_nav_init_dijkstra(ctx, start_node);

    for(step_index = 0U; step_index < ctx->header.node_count; step_index++) {
        current_node = lv_offline_nav_pick_closest_unvisited(ctx);
        if(current_node == LV_OFFLINE_NAV_INVALID_NODE) {
            break;
        }

        ctx->visited[current_node] = true;
        if(current_node == end_node) {
            break;
        }

        lv_offline_nav_relax_outgoing_edges(ctx, current_node);
    }

    if(ctx->distance[end_node] == LV_OFFLINE_NAV_DISTANCE_INF) {
        return false;
    }

    route_ready = lv_offline_nav_build_route(ctx, start_node, end_node);

    return route_ready;
}

/*********************************************************
 * @brief 计算两个路网节点之间的近似地表距离
 * @param a 起点路网节点指针，不能为空
 * @param b 终点路网节点指针，不能为空
 * @return uint32_t 两点之间近似距离，单位为米，参数无效时返回 0
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static uint32_t lv_offline_nav_get_segment_distance_m(const lv_offline_nav_node_t *a,
                                                      const lv_offline_nav_node_t *b)
{
    double lon_a; /* 起点经度，单位为弧度 */
    double lat_a; /* 起点纬度，单位为弧度 */
    double lon_b; /* 终点经度，单位为弧度 */
    double lat_b; /* 终点纬度，单位为弧度 */
    double delta_lon; /* 两点经度差，单位为弧度 */
    double delta_lat; /* 两点纬度差，单位为弧度 */
    double average_lat; /* 两点平均纬度，单位为弧度 */
    double distance_m; /* 两点之间近似地表距离，单位为米 */

    if(a == NULL || b == NULL) {
        return 0U;
    }

    lon_a = ((double)a->lon_e7 / LV_OFFLINE_NAV_LON_LAT_SCALE) * LV_OFFLINE_NAV_PI / 180.0;
    lat_a = ((double)a->lat_e7 / LV_OFFLINE_NAV_LON_LAT_SCALE) * LV_OFFLINE_NAV_PI / 180.0;
    lon_b = ((double)b->lon_e7 / LV_OFFLINE_NAV_LON_LAT_SCALE) * LV_OFFLINE_NAV_PI / 180.0;
    lat_b = ((double)b->lat_e7 / LV_OFFLINE_NAV_LON_LAT_SCALE) * LV_OFFLINE_NAV_PI / 180.0;
    delta_lon = lon_b - lon_a;
    delta_lat = lat_b - lat_a;
    average_lat = (lat_a + lat_b) / 2.0;
    distance_m = sqrt(delta_lon * cos(average_lat) * delta_lon * cos(average_lat) + delta_lat * delta_lat) *
                 LV_OFFLINE_NAV_EARTH_RADIUS_M;
    if(distance_m <= 0.0) {
        return 0U;
    }
    if(distance_m > (double)UINT32_MAX) {
        return UINT32_MAX;
    }

    return (uint32_t)(distance_m + 0.5);
}

/*********************************************************
 * @brief 根据相邻路段方向变化判断转向类型
 * @param ctx 导航规划上下文指针，不能为空
 * @param previous 前一路段起点，不能为空
 * @param current 转向点，不能为空
 * @param next 后一路段终点，不能为空
 * @return lv_offline_nav_turn_type_t 转向类型，角度较小时返回 LV_OFFLINE_NAV_TURN_NONE
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static lv_offline_nav_turn_type_t lv_offline_nav_get_turn_type(const lv_offline_nav_context_t *ctx,
                                                               const lv_offline_nav_node_t *previous,
                                                               const lv_offline_nav_node_t *current,
                                                               const lv_offline_nav_node_t *next)
{
    double in_x; /* 前一路段横向方向分量 */
    double in_y; /* 前一路段纵向方向分量 */
    double out_x; /* 后一路段横向方向分量 */
    double out_y; /* 后一路段纵向方向分量 */
    double cross; /* 两个方向向量叉积，用于判断左转或右转 */
    double dot; /* 两个方向向量点积，用于计算转向夹角 */
    double turn_degree; /* 路线方向变化角度，单位为度 */

    if(ctx == NULL || previous == NULL || current == NULL || next == NULL) {
        return LV_OFFLINE_NAV_TURN_NONE;
    }

    in_x = (double)current->lon_e7 - (double)previous->lon_e7;
    in_y = (double)current->lat_e7 - (double)previous->lat_e7;
    out_x = (double)next->lon_e7 - (double)current->lon_e7;
    out_y = (double)next->lat_e7 - (double)current->lat_e7;
    if((in_x == 0.0 && in_y == 0.0) || (out_x == 0.0 && out_y == 0.0)) {
        return LV_OFFLINE_NAV_TURN_NONE;
    }

    cross = in_x * out_y - in_y * out_x;
    dot = in_x * out_x + in_y * out_y;
    turn_degree = fabs(atan2(cross, dot) * 180.0 / LV_OFFLINE_NAV_PI);
    if(turn_degree < ctx->config.turn_min_degree) {
        return LV_OFFLINE_NAV_TURN_NONE;
    }
    if(turn_degree >= ctx->config.uturn_min_degree) {
        return LV_OFFLINE_NAV_TURN_UTURN;
    }

    return cross > 0.0 ? LV_OFFLINE_NAV_TURN_LEFT : LV_OFFLINE_NAV_TURN_RIGHT;
}

/*********************************************************
 * @brief 根据规划出的路线节点生成导航转向提示输出
 * @param ctx 导航规划上下文指针，不能为空
 * @param route 路线输出对象指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static void lv_offline_nav_build_guidance(lv_offline_nav_context_t *ctx, lv_offline_nav_route_t *route)
{
    uint32_t route_index; /* 当前扫描的路线节点序号 */
    uint32_t prev_index; /* 前一路线节点在路网节点数组中的索引 */
    uint32_t current_index; /* 当前转向候选节点在路网节点数组中的索引 */
    uint32_t next_index; /* 后一路线节点在路网节点数组中的索引 */
    uint32_t segment_distance_m; /* 当前路段的近似地表距离，单位为米 */
    uint32_t accumulated_distance_m; /* 到当前路线节点的累计距离，单位为米 */
    uint16_t event_limit; /* 本次允许写入的最大转向事件数量 */
    lv_offline_nav_turn_type_t turn_type; /* 当前候选节点计算得到的转向类型 */

    if(ctx == NULL || route == NULL) {
        return;
    }

    route->turn_event_count = 0U;
    route->total_distance_m = 0U;
    if(ctx->route_point_count < 2U) {
        return;
    }

    event_limit = route->turn_event_capacity;
    if(event_limit > ctx->config.max_turn_events) {
        event_limit = ctx->config.max_turn_events;
    }

    accumulated_distance_m = 0U;
    for(route_index = 1U; route_index < ctx->route_point_count; route_index++) {
        prev_index = ctx->route_nodes[route_index - 1U];
        current_index = ctx->route_nodes[route_index];
        segment_distance_m = lv_offline_nav_get_segment_distance_m(&ctx->nodes[prev_index],
                                                                    &ctx->nodes[current_index]);
        accumulated_distance_m += segment_distance_m;
        if(route_index + 1U >= ctx->route_point_count) {
            continue;
        }

        next_index = ctx->route_nodes[route_index + 1U];
        turn_type = lv_offline_nav_get_turn_type(ctx, &ctx->nodes[prev_index], &ctx->nodes[current_index],
                                                 &ctx->nodes[next_index]);
        if(turn_type != LV_OFFLINE_NAV_TURN_NONE && route->turn_events != NULL &&
           route->turn_event_count < event_limit) {
            route->turn_events[route->turn_event_count].type = turn_type;
            route->turn_events[route->turn_event_count].distance_m = accumulated_distance_m;
            route->turn_event_count++;
        }
    }

    route->total_distance_m = accumulated_distance_m;
}

/*********************************************************
 * @brief 按路线累计长度采样一个轨迹显示点
 * @param ctx 导航规划上下文指针，不能为空
 * @param target_distance 目标累计里程，单位为米
 * @param sample 输出采样点，不能为空
 * @return bool true 表示采样成功，false 表示路线为空或参数无效
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_sample_route_by_distance(lv_offline_nav_context_t *ctx, uint64_t target_distance,
                                                    lv_offline_nav_point_t *sample)
{
    uint32_t route_index; /* 当前扫描的路线节点索引 */
    uint32_t from_index; /* 当前路段起点在节点数组中的索引 */
    uint32_t to_index; /* 当前路段终点在节点数组中的索引 */
    uint32_t segment_distance_m; /* 当前路段近似地表距离，单位为米 */
    uint64_t accumulated_distance_m; /* 已扫描路段的累计里程，单位为米 */
    double ratio; /* 目标距离落在当前路段内的位置比例 */
    const lv_offline_nav_node_t *from_node; /* 当前采样路段起点节点 */
    const lv_offline_nav_node_t *to_node; /* 当前采样路段终点节点 */

    if(ctx == NULL || sample == NULL || ctx->route_point_count == 0U) {
        return false;
    }

    accumulated_distance_m = 0U;
    for(route_index = 1U; route_index < ctx->route_point_count; route_index++) {
        from_index = ctx->route_nodes[route_index - 1U];
        to_index = ctx->route_nodes[route_index];
        from_node = &ctx->nodes[from_index];
        to_node = &ctx->nodes[to_index];
        segment_distance_m = lv_offline_nav_get_segment_distance_m(from_node, to_node);
        if(accumulated_distance_m + segment_distance_m >= target_distance) {
            ratio = 0.0;
            if(segment_distance_m > 0U) {
                ratio = (double)(target_distance - accumulated_distance_m) / (double)segment_distance_m;
            }
            sample->lon_e7 = from_node->lon_e7 + (int32_t)((double)(to_node->lon_e7 - from_node->lon_e7) * ratio);
            sample->lat_e7 = from_node->lat_e7 + (int32_t)((double)(to_node->lat_e7 - from_node->lat_e7) * ratio);
            sample->distance_m = target_distance > (uint64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)target_distance;
            return true;
        }

        accumulated_distance_m += segment_distance_m;
    }

    to_node = &ctx->nodes[ctx->route_nodes[ctx->route_point_count - 1U]];
    sample->lon_e7 = to_node->lon_e7;
    sample->lat_e7 = to_node->lat_e7;
    sample->distance_m = accumulated_distance_m > (uint64_t)UINT32_MAX ? UINT32_MAX :
                         (uint32_t)accumulated_distance_m;

    return true;
}

/*********************************************************
 * @brief 将规划上下文转换成可移植路线输出对象
 * @param ctx 导航规划上下文指针，不能为空
 * @param route 路线输出对象指针，不能为空
 * @return bool true 表示路线输出成功，false 表示输出容量不足或路线为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
static bool lv_offline_nav_build_route_output(lv_offline_nav_context_t *ctx, lv_offline_nav_route_t *route)
{
    uint32_t render_count; /* 本次写入输出数组的路线点数量 */
    uint32_t render_index; /* 当前渲染的路线点序号 */
    uint32_t route_index; /* 计算总长度时扫描的完整路线节点索引 */
    uint32_t from_index; /* 当前统计路段起点节点索引 */
    uint32_t to_index; /* 当前统计路段终点节点索引 */
    uint32_t accumulated_distance_m; /* 当前输出点对应的累计里程，单位为米 */
    uint32_t segment_distance_m; /* 当前路段近似地表距离，单位为米 */
    uint64_t total_distance; /* 完整路线累计里程，用于按距离均匀采样，单位为米 */
    uint64_t target_distance; /* 当前输出点对应的目标累计距离 */
    lv_offline_nav_point_t sample; /* 当前写入输出数组的采样点 */
    const lv_offline_nav_node_t *center_node; /* 用于建议调用者居中地图的路线中段节点 */
    const lv_offline_nav_node_t *route_node; /* 当前写入输出数组的路网节点 */

    if(ctx == NULL || route == NULL || route->points == NULL || route->point_capacity == 0U ||
       ctx->route_point_count == 0U) {
        return false;
    }

    lv_offline_nav_clear_route(route);
    center_node = &ctx->nodes[ctx->route_nodes[ctx->route_point_count / 2U]];
    route->center_point.lon_e7 = center_node->lon_e7;
    route->center_point.lat_e7 = center_node->lat_e7;
    route->center_point.distance_m = 0U;
    route->has_center_point = true;

    render_count = ctx->route_point_count;
    if(render_count > ctx->config.max_render_points) {
        render_count = ctx->config.max_render_points;
    }
    if(render_count > route->point_capacity) {
        render_count = route->point_capacity;
    }

    total_distance = 0U;
    for(route_index = 1U; route_index < ctx->route_point_count; route_index++) {
        from_index = ctx->route_nodes[route_index - 1U];
        to_index = ctx->route_nodes[route_index];
        total_distance += lv_offline_nav_get_segment_distance_m(&ctx->nodes[from_index], &ctx->nodes[to_index]);
    }
    lv_offline_nav_build_guidance(ctx, route);

    if(render_count == ctx->route_point_count) {
        accumulated_distance_m = 0U;
        for(route_index = 0U; route_index < ctx->route_point_count; route_index++) {
            if(route_index > 0U) {
                from_index = ctx->route_nodes[route_index - 1U];
                to_index = ctx->route_nodes[route_index];
                segment_distance_m = lv_offline_nav_get_segment_distance_m(&ctx->nodes[from_index],
                                                                           &ctx->nodes[to_index]);
                accumulated_distance_m += segment_distance_m;
            }

            route_node = &ctx->nodes[ctx->route_nodes[route_index]];
            route->points[route->point_count].lon_e7 = route_node->lon_e7;
            route->points[route->point_count].lat_e7 = route_node->lat_e7;
            route->points[route->point_count].distance_m = accumulated_distance_m;
            route->point_count++;
        }
    } else {
        for(render_index = 0U; render_index < render_count; render_index++) {
            if(render_count <= 1U || total_distance == 0U) {
                target_distance = 0U;
            } else {
                target_distance = total_distance * render_index / (render_count - 1U);
            }

            if(lv_offline_nav_sample_route_by_distance(ctx, target_distance, &sample)) {
                route->points[route->point_count] = sample;
                route->point_count++;
            }
        }
    }

    if(route->point_count == 0U) {
        lv_offline_nav_clear_route(route);
        return false;
    }

    return true;
}

/*********************************************************
 * @brief 获取离线导航库默认配置
 * @param config 输出配置指针，不能为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_nav_get_default_config(lv_offline_nav_config_t *config)
{
    if(config == NULL) {
        return;
    }

    config->graph_path = LV_OFFLINE_NAV_DEFAULT_GRAPH_PATH;
    config->max_nodes = LV_OFFLINE_NAV_DEFAULT_MAX_NODES;
    config->max_edges = LV_OFFLINE_NAV_DEFAULT_MAX_EDGES;
    config->max_route_points = LV_OFFLINE_NAV_DEFAULT_MAX_ROUTE_POINTS;
    config->max_render_points = LV_OFFLINE_NAV_DEFAULT_MAX_RENDER_POINTS;
    config->max_turn_events = LV_OFFLINE_NAV_DEFAULT_MAX_TURN_EVENTS;
    config->turn_min_degree = LV_OFFLINE_NAV_DEFAULT_TURN_MIN_DEGREE;
    config->uturn_min_degree = LV_OFFLINE_NAV_DEFAULT_UTURN_MIN_DEGREE;
    config->malloc_cb = NULL;
    config->free_cb = NULL;
    config->mem_user_data = NULL;
}

/*********************************************************
 * @brief 清空路线输出状态但保留调用者提供的数组指针和容量
 * @param route 路线输出对象指针，允许为空
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
void lv_offline_nav_clear_route(lv_offline_nav_route_t *route)
{
    lv_offline_nav_point_t *points; /* 调用者提供的路线采样点数组指针 */
    uint32_t point_capacity; /* 调用者提供的路线采样点数组容量 */
    lv_offline_nav_turn_event_t *turn_events; /* 调用者提供的转向事件数组指针 */
    uint16_t turn_event_capacity; /* 调用者提供的转向事件数组容量 */

    if(route == NULL) {
        return;
    }

    points = route->points;
    point_capacity = route->point_capacity;
    turn_events = route->turn_events;
    turn_event_capacity = route->turn_event_capacity;
    memset(route, 0, sizeof(*route));
    route->points = points;
    route->point_capacity = point_capacity;
    route->turn_events = turn_events;
    route->turn_event_capacity = turn_event_capacity;
}

/*********************************************************
 * @brief 按起终点 E7 经纬度规划离线路线
 * @param config 导航库配置指针，允许为空，空值使用默认配置
 * @param start_lon_e7 起点经度 E7 定点值，单位为 1e-7 度
 * @param start_lat_e7 起点纬度 E7 定点值，单位为 1e-7 度
 * @param end_lon_e7 终点经度 E7 定点值，单位为 1e-7 度
 * @param end_lat_e7 终点纬度 E7 定点值，单位为 1e-7 度
 * @param route 输出路线对象，不能为空，points 和容量必须有效
 * @return bool true 表示路线规划成功，false 表示路网不可用、终点不可达或输出容量不足
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_nav_plan_route(const lv_offline_nav_config_t *config, int32_t start_lon_e7,
                               int32_t start_lat_e7, int32_t end_lon_e7, int32_t end_lat_e7,
                               lv_offline_nav_route_t *route)
{
    lv_offline_nav_context_t context; /* 本次路径规划的临时上下文，规划结束后立即释放动态缓存 */
    lv_offline_nav_config_t normalized_config; /* 修正后的导航配置副本 */
    uint32_t start_node; /* 起点经纬度吸附到的最近路网节点索引 */
    uint32_t end_node; /* 终点经纬度吸附到的最近路网节点索引 */
    bool route_ready; /* 路线规划和输出结果，true 表示 route 已填充有效采样点 */

    if(route == NULL || route->points == NULL || route->point_capacity == 0U) {
        return false;
    }

    if(config == NULL) {
        lv_offline_nav_get_default_config(&normalized_config);
    } else {
        normalized_config = *config;
    }
    lv_offline_nav_normalize_config(&normalized_config);

    memset(&context, 0, sizeof(context));
    context.config = normalized_config;
    route_ready = false;
    lv_offline_nav_clear_route(route);

    if(lv_offline_nav_load_graph(&context)) {
        start_node = lv_offline_nav_find_nearest_node(&context, start_lon_e7, start_lat_e7);
        end_node = lv_offline_nav_find_nearest_node(&context, end_lon_e7, end_lat_e7);
        printf("[nav-debug] plan_route: start_node=%lu end_node=%lu "
              "(invalid=%lu)\n", (unsigned long)start_node,
              (unsigned long)end_node,
              (unsigned long)LV_OFFLINE_NAV_INVALID_NODE);
        if(start_node != LV_OFFLINE_NAV_INVALID_NODE && end_node != LV_OFFLINE_NAV_INVALID_NODE) {
            if(!lv_offline_nav_plan_route_by_node(&context, start_node, end_node)) {
                printf("[nav-debug] plan_route: plan_route_by_node failed "
                      "(no path between nodes)\n");
            } else {
                route_ready = lv_offline_nav_build_route_output(&context, route);
                printf("[nav-debug] plan_route: build_route_output=%d "
                      "point_count=%u\n", (int)route_ready,
                      (unsigned int)route->point_count);
            }
        }
    } else {
        printf("[nav-debug] plan_route: load_graph failed\n");
    }

    lv_offline_nav_free_context(&context);
    if(!route_ready) {
        lv_offline_nav_clear_route(route);
    }

    return route_ready;
}

/*********************************************************
 * @brief 根据已行驶里程查询当前路线导航提示
 * @param route 已规划成功的路线对象，不能为空
 * @param traveled_distance_m 当前已行驶里程，单位为米
 * @param guidance 输出导航提示状态，不能为空
 * @return bool true 表示存在有效导航路线，false 表示当前没有可用路线
 * @version 1.0
 * @author ^^^^^^^ ()
 * @date 2026-06-22
 * @copyright Copyright (c) &&&&&&&&& Technologies Company 2026
 *********************************************************/
bool lv_offline_nav_get_guidance(const lv_offline_nav_route_t *route, uint32_t traveled_distance_m,
                                 lv_offline_nav_guidance_t *guidance)
{
    uint16_t event_index; /* 当前扫描的转向事件索引 */

    if(route == NULL || guidance == NULL) {
        return false;
    }

    guidance->turn_type = LV_OFFLINE_NAV_TURN_NONE;
    guidance->distance_to_turn_m = 0U;
    guidance->total_distance_m = route->total_distance_m;
    guidance->traveled_distance_m = traveled_distance_m;
    if(route->total_distance_m > traveled_distance_m) {
        guidance->remaining_distance_m = route->total_distance_m - traveled_distance_m;
    } else {
        guidance->remaining_distance_m = 0U;
    }

    if(route->total_distance_m == 0U || route->point_count == 0U) {
        return false;
    }

    for(event_index = 0U; event_index < route->turn_event_count; event_index++) {
        if(route->turn_events[event_index].distance_m >= traveled_distance_m) {
            guidance->turn_type = route->turn_events[event_index].type;
            guidance->distance_to_turn_m = route->turn_events[event_index].distance_m - traveled_distance_m;
            break;
        }
    }

    return true;
}
