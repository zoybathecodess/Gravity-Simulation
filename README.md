# 🌌 Gravity Simulation Engine  
*A real-time orbital mechanics simulator built from scratch using C++ and OpenGL*

---

## Overview

This project is a real-time gravity simulation engine built completely from scratch as a weekend side project exploring the intersection of:

- Physics
- Mathematics
- Computer Graphics
- Numerical Simulation
- Systems Programming

What started as a simple OpenGL experiment—"can I render something on screen?"—quickly evolved into building a miniature physics engine capable of simulating gravitational interactions between celestial bodies.

Instead of relying on existing game engines or physics libraries, every part of this project was implemented manually:

- Window creation
- OpenGL rendering pipeline
- Shader setup
- Physics calculations
- Numerical integration
- Real-time simulation loop

The goal of this project is to visualize how physical laws can be translated directly into software and rendered frame-by-frame.

---

# Project Architecture
```
gravity_sim/
│
├── gravity_sim.cpp        # Main simulation source
├── glad.c                 # OpenGL loader
├── include/               # Header files
├── shaders/               # Shader sources (future)
├── assets/                # Textures/models (future)
├── README.md
```
---

# Demo

Current simulation supports:

✅ Two-body gravitational interaction  
✅ Planet-star orbital systems  
✅ Real-time motion updates  
✅ GPU rendering using OpenGL  
✅ Numerically integrated orbital motion  

How to Run:
1. Download all files from the repo
2. Download MinGW Compiler
3. Compile

Open a terminal inside your project folder and run:

```bash
g++ <path-to-project>/gravity_sim.cpp <path-to-project>/glad.c ^
-I<path-to-project>/include ^
-L<path-to-project>/lib ^
-o gravity_sim.exe ^
-lglfw3 -lopengl32 -lgdi32 -luser32 -lkernel32
```
4. Execute
```bash
gravity_sim.exe
```

---

# The Science Behind It

## Newton's Law of Universal Gravitation

The entire simulation is based on Newton's gravitational equation:

F = G(m₁m₂)/r²

Where:

- **F** = gravitational force
- **G** = gravitational constant
- **m₁, m₂** = masses of two bodies
- **r** = distance between them

This equation determines how strongly two objects attract each other.

---

## Newton's Second Law

Force is converted into acceleration using:

F = ma

Which gives:

a = F/m

This acceleration changes an object's velocity over time.

---

## Motion Equations

Each simulation frame updates:

### Velocity

v = v + aΔt

### Position

x = x + vΔt

Where:

- **v** = velocity
- **a** = acceleration
- **x** = position
- **Δt** = timestep

This allows continuous motion to be approximated using discrete frames.

---

# Numerical Methods

## Why Numerical Integration?

Real planetary motion is continuous.

Computers simulate this by splitting time into small steps.

This project currently uses:

## Symplectic Euler Integration

Instead of standard Euler:

```cpp
x += v * dt;
v += a * dt;
