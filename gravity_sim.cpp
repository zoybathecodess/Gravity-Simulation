#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cmath>


// ─── SHADERS ─────────────────────────────────────────────────────────────────

const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out float lightIntensity;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vec3 worldNormal = normalize(mat3(transpose(inverse(model))) * aPos);
    vec3 lightDir    = normalize(vec3(1.0, 2.0, 1.5));
    float diff       = max(dot(worldNormal, lightDir), 0.0);
    lightIntensity   = 0.18 + 0.82 * diff;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in float lightIntensity;
out vec4 FragColor;

uniform vec4 objectColor;
uniform bool isGlow;
uniform bool isGrid;

void main() {
    if (isGrid) {
        FragColor = objectColor;
    } else if (isGlow) {
        float shade = mix(0.35, 1.0, lightIntensity);
        FragColor = vec4(objectColor.rgb * shade, objectColor.a);
    } else {
        float fade = smoothstep(0.0, 10.0, lightIntensity * 10.0);
        FragColor = vec4(objectColor.rgb * fade, objectColor.a);
    }
}
)";

// ─── GLOBALS ─────────────────────────────────────────────────────────────────

// Eye-level: camera sits just above the grid plane, looks straight forward.
glm::vec3 cameraPos   = glm::vec3(0.0f, 0.3f, 2.5f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f,  0.0f);

float yaw       = -90.0f;
float pitch     =   0.0f;
float lastX     = 400.0f;
float lastY     = 300.0f;
float deltaTime =   0.0f;
float lastFrame =   0.0f;

// ─── BODY ────────────────────────────────────────────────────────────────────

struct Body {
    glm::vec3 pos;      // position on XZ plane (y used only for rendering height)
    glm::vec3 vel;      // velocity (x, z components used for physics)
    float     mass;     // visual mass — drives warp depth and sphere radius
    glm::vec4 color;
    bool      isFixed;  // if true, body doesn't move (e.g. central star)

    // VAO/VBO for this body's sphere mesh
    unsigned int VAO, VBO;
    int          vertCount;
};

// All live bodies
std::vector<Body> bodies;

// ─── HELPERS ─────────────────────────────────────────────────────────────────

unsigned int CompileShader(GLenum type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        std::cerr << "Shader error: " << log << "\n";
    }
    return s;
}

void MakeVAO(const std::vector<float>& verts, GLenum usage,
             unsigned int& outVAO, unsigned int& outVBO) {
    glGenVertexArrays(1, &outVAO);
    glGenBuffers(1, &outVBO);
    glBindVertexArray(outVAO);
    glBindBuffer(GL_ARRAY_BUFFER, outVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), usage);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ─── SPHERE GEOMETRY ─────────────────────────────────────────────────────────

std::vector<float> GenerateSphere(float radius, int stacks = 20, int sectors = 20) {
    std::vector<float> verts;
    for (int i = 0; i < stacks; ++i) {
        float t1 = ((float)i       / stacks)  * (float)M_PI;
        float t2 = ((float)(i + 1) / stacks)  * (float)M_PI;
        for (int j = 0; j < sectors; ++j) {
            float p1 = ((float)j       / sectors) * 2.0f * (float)M_PI;
            float p2 = ((float)(j + 1) / sectors) * 2.0f * (float)M_PI;

            glm::vec3 v1 = { radius*sinf(t1)*cosf(p1), radius*cosf(t1), radius*sinf(t1)*sinf(p1) };
            glm::vec3 v2 = { radius*sinf(t1)*cosf(p2), radius*cosf(t1), radius*sinf(t1)*sinf(p2) };
            glm::vec3 v3 = { radius*sinf(t2)*cosf(p1), radius*cosf(t2), radius*sinf(t2)*sinf(p1) };
            glm::vec3 v4 = { radius*sinf(t2)*cosf(p2), radius*cosf(t2), radius*sinf(t2)*sinf(p2) };

            verts.insert(verts.end(), {v1.x,v1.y,v1.z, v2.x,v2.y,v2.z, v3.x,v3.y,v3.z});
            verts.insert(verts.end(), {v2.x,v2.y,v2.z, v4.x,v4.y,v4.z, v3.x,v3.y,v3.z});
        }
    }
    return verts;
}

// Sphere radius scales with mass so heavier bodies look bigger.
// Reference uses density-based radius; we keep it simple: r = base * cbrt(mass).
float BodyRadius(float mass) {
    return 0.035f * cbrtf(mass);
}

// Allocate GPU buffers for a body's sphere mesh.
void InitBodyMesh(Body& b) {
    float r = BodyRadius(b.mass);
    std::vector<float> verts = GenerateSphere(r);
    b.vertCount = (int)verts.size() / 3;
    MakeVAO(verts, GL_STATIC_DRAW, b.VAO, b.VBO);
}

// ─── GRID ────────────────────────────────────────────────────────────────────

std::vector<float> CreateGrid(float size, int divisions) {
    std::vector<float> verts;
    float step = size / divisions;
    float half = size / 2.0f;
    for (int z = 0; z <= divisions; ++z) {
        float zp = -half + z * step;
        for (int x = 0; x < divisions; ++x) {
            float x0 = -half + x * step;
            verts.insert(verts.end(), {x0,        0.0f, zp});
            verts.insert(verts.end(), {x0 + step, 0.0f, zp});
        }
    }
    for (int x = 0; x <= divisions; ++x) {
        float xp = -half + x * step;
        for (int z = 0; z < divisions; ++z) {
            float z0 = -half + z * step;
            verts.insert(verts.end(), {xp, 0.0f, z0});
            verts.insert(verts.end(), {xp, 0.0f, z0 + step});
        }
    }
    return verts;
}

// ─── WARP ────────────────────────────────────────────────────────────────────
//
// Reference-style shallow, broad funnel:
//
//   dip = wellDepth / sqrt(dist^2 + softening^2)
//
// Using sqrt instead of plain dist gives a much smoother, broader depression —
// the denominator never goes below `softening`, so the tip stays flat rather
// than spiking to infinity. This matches the reference video's look.
//
// Parameters tuned to reference:
//   wellDepth  = 0.18  → gentle max dip (~0.18 units at centre for a star)
//   softening  = 0.35  → broad, wide funnel (larger = wider and shallower tip)
//
// Each body contributes independently; the total is summed and clamped.

static const float WELL_DEPTH  = 0.18f;
static const float SOFTENING   = 0.35f;
static const float MAX_DIP     = 0.55f;  // prevents grid from folding

void WarpGrid(std::vector<float>& verts, const std::vector<Body>& bods) {
    // Find max mass for normalisation
    float maxMass = 1.0f;
    for (int i = 0; i < (int)bods.size(); ++i)
        if (bods[i].mass > maxMass) maxMass = bods[i].mass;

    for (int i = 0; i < (int)verts.size(); i += 3) {
        float px = verts[i];
        float pz = verts[i + 2];
        float totalDip = 0.0f;

        for (int j = 0; j < (int)bods.size(); ++j) {
            float dx   = bods[j].pos.x - px;
            float dz   = bods[j].pos.z - pz;
            float dist2 = dx*dx + dz*dz;
            // Smooth inverse-distance: 1/sqrt(d^2 + s^2)
            float dip = (bods[j].mass / maxMass) * WELL_DEPTH
                        / sqrtf(dist2 + SOFTENING * SOFTENING);
            totalDip += dip;
        }

        if (totalDip > MAX_DIP) totalDip = MAX_DIP;
        verts[i + 1] = -totalDip;
    }
}

// ─── PHYSICS ─────────────────────────────────────────────────────────────────
//
// Leapfrog (kick-drift-kick) N-body integration in the XZ plane.
// Leapfrog conserves energy far better than Euler for orbits —
// planets stay on stable ellipses instead of slowly spiraling out.
//
// Force softening: we clamp the effective distance to MIN_DIST so two
// bodies never produce infinite force when they overlap.  The orbital
// velocity in SpawnPlanet uses the SAME effective distance so the initial
// speed is in exact equilibrium with the force.

static const float G_SIM    = 0.0008f;
static const float DT       = 0.004f;   // smaller step → less energy drift
static const float MIN_DIST = 0.12f;    // softening clamp — same used in SpawnPlanet

// Returns acceleration on body i due to body j (XZ plane only).
static glm::vec3 CalcAcc(int i, int j) {
    float dx      = bodies[j].pos.x - bodies[i].pos.x;
    float dz      = bodies[j].pos.z - bodies[i].pos.z;
    float rawDist = sqrtf(dx*dx + dz*dz);
    float effDist = (rawDist < MIN_DIST) ? MIN_DIST : rawDist;   // softened
    float f       = G_SIM * bodies[j].mass / (effDist * effDist);
    // Direction: normalise using raw (dx,dz) but magnitude from softened dist
    float invLen  = (rawDist > 0.0001f) ? (1.0f / rawDist) : 0.0f;
    return glm::vec3(dx * invLen * f, 0.0f, dz * invLen * f);
}

void StepPhysics() {
    int n = (int)bodies.size();

    // Kick: half-step velocity update
    std::vector<glm::vec3> acc(n, glm::vec3(0.0f));
    for (int i = 0; i < n; ++i) {
        if (bodies[i].isFixed) continue;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            acc[i] += CalcAcc(i, j);
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!bodies[i].isFixed)
            bodies[i].vel += acc[i] * (DT * 0.5f);
    }

    // Drift: full-step position update
    for (int i = 0; i < n; ++i) {
        if (!bodies[i].isFixed)
            bodies[i].pos += bodies[i].vel * DT;
    }

    // Kick: second half-step velocity update with new positions
    std::fill(acc.begin(), acc.end(), glm::vec3(0.0f));
    for (int i = 0; i < n; ++i) {
        if (bodies[i].isFixed) continue;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            acc[i] += CalcAcc(i, j);
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!bodies[i].isFixed)
            bodies[i].vel += acc[i] * (DT * 0.5f);
    }
}

// ─── SPAWN HELPERS ───────────────────────────────────────────────────────────

// Colour palette for spawned planets
static const glm::vec4 PALETTE[] = {
    {0.0f, 1.0f, 1.0f, 1.0f},   // cyan
    {1.0f, 0.4f, 0.1f, 1.0f},   // orange
    {0.5f, 1.0f, 0.3f, 1.0f},   // green
    {0.9f, 0.3f, 0.9f, 1.0f},   // magenta
    {1.0f, 0.85f, 0.2f, 1.0f},  // yellow
    {0.3f, 0.6f, 1.0f, 1.0f},   // blue
};
static int paletteIdx = 0;

// Spawn a planet at a given XZ position with a given mass.
// Gives it a tangential velocity so it naturally orbits the nearest fixed body.
void SpawnPlanet(float px, float pz, float mass) {
    // Find nearest fixed body to orbit around
    float nearestDist = 1e9f;
    glm::vec3 center(0.0f);
    float centerMass = 1.0f;
    for (int i = 0; i < (int)bodies.size(); ++i) {
        if (!bodies[i].isFixed) continue;
        float d = glm::length(bodies[i].pos - glm::vec3(px, 0.0f, pz));
        if (d < nearestDist) {
            nearestDist = d;
            center      = bodies[i].pos;
            centerMass  = bodies[i].mass;
        }
    }

    // Orbital velocity using the SAME effective distance as CalcAcc.
    // If we used raw nearestDist, the computed speed wouldn't match the
    // actual softened force and the orbit would drift immediately.
    float effR = (nearestDist < MIN_DIST) ? MIN_DIST : nearestDist;
    float v    = sqrtf(G_SIM * centerMass / effR);

    // Tangential direction (perpendicular to radial, in XZ plane)
    float dx  = px - center.x;
    float dz  = pz - center.z;
    float len = sqrtf(dx*dx + dz*dz) + 0.001f;
    glm::vec3 tangent = glm::vec3(-dz / len, 0.0f, dx / len);

    Body b;
    b.pos     = glm::vec3(px, 0.0f, pz);
    b.vel     = tangent * v;
    b.mass    = mass;
    b.color   = PALETTE[paletteIdx % 6];
    b.isFixed = false;
    paletteIdx++;
    InitBodyMesh(b);
    bodies.push_back(b);
    std::cout << "Spawned planet at (" << px << ", " << pz
              << ") mass=" << mass << " orb_v=" << v << "\n";
}

// ─── CALLBACKS ───────────────────────────────────────────────────────────────

// Track key state for mass increase while held
bool keyNPressed    = false;
float spawnMass     = 1.5f;   // default spawn mass (adjustable with +/-)

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    float xoffset = (float)(xpos - lastX) * 0.08f;
    float yoffset = (float)(lastY - ypos) * 0.08f;
    lastX = (float)xpos;
    lastY = (float)ypos;

    yaw   += xoffset;
    pitch += yoffset;
    if (pitch >  89.0f) pitch =  89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front.y = sinf(glm::radians(pitch));
    front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    cameraPos += (float)yoffset * 0.12f * cameraFront;
}

// ─── MAIN ────────────────────────────────────────────────────────────────────

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Orbit Simulation", NULL, NULL);
    glfwMaximizeWindow(window);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // ── Shaders ──
    unsigned int vs   = CompileShader(GL_VERTEX_SHADER,   vertexShaderSource);
    unsigned int fs   = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);

    // ── Grid ──
    const float GRID_SIZE = 6.0f;
    const int   GRID_DIVS = 60;
    std::vector<float> gridVerts = CreateGrid(GRID_SIZE, GRID_DIVS);
    unsigned int gridVAO, gridVBO;
    MakeVAO(gridVerts, GL_DYNAMIC_DRAW, gridVAO, gridVBO);

    // ── Uniform locations ──
    glUseProgram(prog);
    GLint modelLoc = glGetUniformLocation(prog, "model");
    GLint viewLoc  = glGetUniformLocation(prog, "view");
    GLint projLoc  = glGetUniformLocation(prog, "projection");
    GLint colorLoc = glGetUniformLocation(prog, "objectColor");
    GLint glowLoc  = glGetUniformLocation(prog, "isGlow");
    GLint gridLoc  = glGetUniformLocation(prog, "isGrid");

    // ── Initial scene: one fixed star + two orbiting planets ──
    {
        // Central star — fixed, heavy, yellow
        Body star;
        star.pos     = glm::vec3(0.0f, 0.0f, 0.0f);
        star.vel     = glm::vec3(0.0f);
        star.mass    = 8.0f;
        star.color   = glm::vec4(1.0f, 0.93f, 0.18f, 1.0f);
        star.isFixed = true;
        InitBodyMesh(star);
        bodies.push_back(star);
    }

    // Two planets at different orbits — spawned via helper so they get
    // correct tangential velocity automatically
    SpawnPlanet( 0.55f, 0.0f, 1.0f);   // inner planet, cyan
    SpawnPlanet(-1.1f,  0.2f, 1.5f);   // outer planet, orange

    std::cout << "\n=== CONTROLS ===\n"
              << "WASD + mouse : fly camera\n"
              << "Scroll       : zoom\n"
              << "Space / Shift: move up / down\n"
              << "N            : spawn planet at a random near-orbit position\n"
              << "+  /  -      : increase / decrease next spawn mass (current: "
              << spawnMass << ")\n"
              << "X            : remove last spawned planet\n"
              << "Escape       : quit\n\n";

    // ── Render loop ──
    while (!glfwWindowShouldClose(window)) {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame;
        lastFrame = now;

        // ── Input ──
        float spd = 1.5f * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_W)          == GLFW_PRESS) cameraPos += spd * cameraFront;
        if (glfwGetKey(window, GLFW_KEY_S)          == GLFW_PRESS) cameraPos -= spd * cameraFront;
        if (glfwGetKey(window, GLFW_KEY_A)          == GLFW_PRESS)
            cameraPos -= spd * glm::normalize(glm::cross(cameraFront, cameraUp));
        if (glfwGetKey(window, GLFW_KEY_D)          == GLFW_PRESS)
            cameraPos += spd * glm::normalize(glm::cross(cameraFront, cameraUp));
        if (glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS) cameraPos += spd * cameraUp;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) cameraPos -= spd * cameraUp;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE)     == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // N — spawn planet (edge-triggered)
        bool nDown = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
        if (nDown && !keyNPressed) {
            // Random angle, radius between 0.4 and 1.4
            float angle = ((float)rand() / RAND_MAX) * 2.0f * (float)M_PI;
            float rad   = 0.4f + ((float)rand() / RAND_MAX) * 1.0f;
            SpawnPlanet(rad * cosf(angle), rad * sinf(angle), spawnMass);
        }
        keyNPressed = nDown;

        // +/- — adjust spawn mass
        if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS) {
            spawnMass += 0.5f * deltaTime;
            if (spawnMass > 6.0f) spawnMass = 6.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS) {
            spawnMass -= 0.5f * deltaTime;
            if (spawnMass < 0.2f) spawnMass = 0.2f;
        }

        // X — remove last non-fixed body (edge-triggered via static)
        static bool xWasDown = false;
        bool xDown = glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS;
        if (xDown && !xWasDown) {
            // walk backwards and remove the last non-fixed body
            for (int i = (int)bodies.size() - 1; i >= 0; --i) {
                if (!bodies[i].isFixed) {
                    glDeleteVertexArrays(1, &bodies[i].VAO);
                    glDeleteBuffers(1, &bodies[i].VBO);
                    bodies.erase(bodies.begin() + i);
                    std::cout << "Removed last planet.\n";
                    break;
                }
            }
        }
        xWasDown = xDown;

        // ── Physics ──
        // Run multiple sub-steps per frame for stability
        // 2 leapfrog substeps per frame — stable and cheap
        StepPhysics();
        StepPhysics();

        // ── Warp grid ──
        gridVerts = CreateGrid(GRID_SIZE, GRID_DIVS);
        WarpGrid(gridVerts, bodies);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float),
                     gridVerts.data(), GL_DYNAMIC_DRAW);

        // ── Render ──
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // pure black
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (h == 0) h = 1;
        glViewport(0, 0, w, h);

        glm::mat4 proj = glm::perspective(glm::radians(50.0f), (float)w / (float)h, 0.01f, 200.0f);
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

        // Grid — white, slightly transparent
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 0.65f);
        glUniform1i(glowLoc, 0);
        glUniform1i(gridLoc, 1);
        glBindVertexArray(gridVAO);
        glDrawArrays(GL_LINES, 0, (GLsizei)(gridVerts.size() / 3));

        // Bodies
        for (int i = 0; i < (int)bodies.size(); ++i) {
            // Sit each body just above the warped grid surface at its position
            float bodyY = BodyRadius(bodies[i].mass) + 0.005f;
            model = glm::translate(glm::mat4(1.0f),
                        glm::vec3(bodies[i].pos.x, bodyY, bodies[i].pos.z));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glUniform4f(colorLoc,
                bodies[i].color.r, bodies[i].color.g,
                bodies[i].color.b, bodies[i].color.a);
            glUniform1i(glowLoc, 1);
            glUniform1i(gridLoc, 0);
            glBindVertexArray(bodies[i].VAO);
            glDrawArrays(GL_TRIANGLES, 0, bodies[i].vertCount);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    for (int i = 0; i < (int)bodies.size(); ++i) {
        glDeleteVertexArrays(1, &bodies[i].VAO);
        glDeleteBuffers(1, &bodies[i].VBO);
    }
    glDeleteVertexArrays(1, &gridVAO);
    glDeleteBuffers(1, &gridVBO);
    glDeleteProgram(prog);
    glfwTerminate();
    return 0;
}