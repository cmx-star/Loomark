<script setup lang="ts" generic="T extends Record<string, any> = any">
import { UTable, UTableColumn } from "@u-design/u-element"
import type { UTableColumnProps } from "@u-design/u-element"

// 1. 定义列配置项类型
export type SchemaColumn<RecordType extends Record<string, any> = any> = Omit<
  UTableColumnProps<RecordType>,
  "prop"
> & {
  prop?: (keyof RecordType & string) | (string & {})
  slot?: string
}

// 2. Props 定义：泛型 T 落在 data 和 columns 上！
const props = withDefaults(
  defineProps<{
    data?: T[]
    columns?: SchemaColumn<T>[]
    loading?: boolean
  }>(),
  {
    data: () => [],
    columns: () => [],
    loading: false,
  },
)

// 3. 动态插槽定义：把 T 精准赋给外部的 #slot="{ row }"
defineSlots<{
  [key: string]: (scope: { row: T; column: any; $index: number }) => any
}>()
</script>

<template>
  <u-table :data="data" v-loading="loading" v-bind="$attrs">
    <!-- 循环 columns 数组，渲染 u-table-column -->
    <template
      v-for="(col, index) in columns"
      :key="col.prop || col.slot || index"
    >
      <u-table-column v-bind="col">
        <!-- 如果配置了 slot，则将数据向外层（页面）抛出 -->
        <template #default="scope" v-if="col.slot">
          <!-- 强类型转换，保证把正确的 row: T 传给页面 -->
          <slot
            :name="col.slot"
            v-bind="scope as { row: T; column: any; $index: number }"
          />
        </template>
      </u-table-column>
    </template>

    <!-- 透传非列插槽（如 #empty 等） -->
    <template v-for="(_, name) in $slots" #[name]="slotProps">
      <slot :name="name" v-bind="slotProps || {}" />
    </template>
  </u-table>
</template>
