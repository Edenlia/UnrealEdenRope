# Stretch Shear Constraint

## Overview

Stretch Shear Constraint定义在两段连续的segment上，考虑连续三个顶点 $p_0,p_1,p_2$，两个segment段$e_0,e_1$（$e_0$代表$p_0,p_1$间的segment）

**Constraint info:**

- 维度: $C\in\mathbb{R}^3$
- Unit: [1] 无量纲
- 等式约束

**Lambda info:**

- 维度: $\Delta\lambda\in\mathbb{R}^3$
- Unit: 

计算所需数据:

- 顶点位置: $\mathbf{x}_0,\mathbf{x}_1,\mathbf{x}_2$
- segment的旋转四元数: $q_0,q_1$
- Rod的总长度: $l$

计算公式:

$C(\mathbf{x}_0,\mathbf{x}_1,q_0)=\frac1l(\mathbf{x}_1-\mathbf{x}_0)-\mathbf{R}(q_0)\mathbf{e}^3=\mathbf{0}$

偏导：

- $J_{\mathbf{x}_0}=-\frac1l\mathbf{I}\in\mathbb{R}^{3\times3}$
- $J_{\mathbf{x}_1}=+\frac1l\mathbf{I}\in\mathbb{R}^{3\times3}$
- $J_{q_0}=\frac{\partial{}}{\partial{q_0}}(-\mathbf{R}(q_0)\mathbf{e}^3)=-2(\mathrm{Im}(\mathbf{e}^3\bar{q}_0)\;|\;\mathrm{Re}(\mathbf{e}^3\bar{q}_0)\cdot\mathbf{I}-[\mathrm{Im}(\mathbf{e}^3\bar{q}_0)]_\times)=-2[\hat{Q}(\mathbf{e}^3\bar{q}_0)]_{3\times4}\in\mathbb{R}^{3\times4}$



## Lambda

# 

## Reference

Position and Orientation Based Cosserat Rods (POBCR): https://animation.rwth-aachen.de/media/papers/2016-SCA-Cosserat-Rods.pdf