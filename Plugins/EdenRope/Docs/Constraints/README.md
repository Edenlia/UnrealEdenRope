# Constraints README

## Overview

此文件夹下的子文档记录了所有约束的公式和解算流程，供后续修改维护

## Constraint Category

本插件有两种类型的绳子建模，分别是：

- Rope：模拟软绳
- Rod：模拟能够扭转的弹性杆或硬杆

根据类型不同，将Constraints分到三个Folder下：

1. External：Rope&Rod的外部约束
2. RopeInternal：Rope材质建模的内部约束
3. RodInternal：Rod材质建模的内部约束

```tex
├── Constraints/
│   ├── External/                              # Rope & Rod 共用的外部约束
│   │   ├── XPBDPinConstraint.h/cpp            # Pin 约束（粒子附着刚体/世界点）
│   │   ├── XPBDCollisionConstraint.h/cpp      # 粒子-刚体碰撞约束
│   │   ├── XPBDSegmentCollisionConstraint.h/cpp # 线段-刚体碰撞约束
│   │   └── XPBDGroundCollisionConstraint.h/cpp  # 地面碰撞约束
│   │
│   ├── RopeInternal/                          # Rope 材质建模内部约束
│   │   ├── XPBDDistanceConstraint.h/cpp       # 距离约束（拉伸）
│   │   ├── XPBDIsometricBendConstraint.h/cpp  # 等距弯曲约束
│   │   └── XPBDProjectionBendConstraint.h/cpp # 投影弯曲约束
│   │
│   └── RodInternal/                           # Rod 材质建模内部约束
│       ├── XPBDStretchShearConstraint.h/cpp   # 拉伸-剪切约束
│       └── XPBDBendTwistConstraint.h/cpp      # 弯曲-扭转约束
```

## Constraint Details

文件夹下的单个Constraint文档需要包含以下内容：

1. Constraint信息
   1. 计算公式
   2. 对各自变量的偏导
   3. 等式约束还是不等式约束
   4. 约束维度
   5. Unit

2. Lambda信息
   1. 计算公式
   2. 维度（能从Constraint维度推得）
   3. Unit

3. Compliance信息
   1. 维度（能从Constraint维度推得）
   2. Unit
4. 解算步骤

*Unit使用 [L],[M],[T] 代替 长度,质量,时间 单位*
