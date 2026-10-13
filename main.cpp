// =============================================================================
//  ZUMA - recriação do clássico da PopCap em C++17 + SFML 3
//
//  Controles:
//    Mouse ............ mira o sapo
//    Clique esquerdo .. atira a bola
//    Clique direito ... troca a bola da boca com a das costas (Espaço também)
//    P / Esc .......... pausa
// =============================================================================
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "estruturas.hpp"

namespace {

// ----------------------------------------------------------------------------
//  Constantes
// ----------------------------------------------------------------------------
constexpr float SCREEN_W = 640.f;          // resolução lógica (a do mapa)
constexpr float SCREEN_H = 480.f;
constexpr unsigned WINDOW_W = 960;         // janela 1.5x
constexpr unsigned WINDOW_H = 720;
constexpr float FIXED_DT = 1.f / 120.f;    // passo fixo da simulação
constexpr float PI = 3.14159265358979f;

// Bolas (folha balls.png: 6 colunas de cor x até 51 quadros de rotação)
constexpr int BALL_FRAME = 32;
constexpr float BALL_D = 32.f;
constexpr float BALL_R = BALL_D / 2.f;
constexpr int COLOR_COUNT = 6;
constexpr std::array<int, COLOR_COUNT> BALL_FRAMES = {47, 50, 51, 50, 50, 50};
const std::array<sf::Color, COLOR_COUNT> BALL_TINT = {
    sf::Color(60, 140, 255), sf::Color(60, 210, 70),  sf::Color(210, 90, 230),
    sf::Color(240, 50, 50),  sf::Color(235, 230, 210), sf::Color(255, 225, 40)};

// Corrente
constexpr float CONNECT_EPS = 1.f;         // folga para considerar bolas encostadas
constexpr float INSERT_TIME = 0.14f;       // animação da bola entrando na corrente
constexpr float HIT_DIST = BALL_D * 0.9f;  // distância de colisão tiro x corrente
constexpr float PULL_ACC = 900.f;          // aceleração da "atração" entre cores iguais
constexpr float PULL_MAX = 520.f;
constexpr float RUSH_SPEED = 240.f;        // entrada rápida no começo da fase
constexpr float RUSH_FRACTION = 0.28f;     // até onde a corrente entra rápido
constexpr float LOSE_SPEED = 520.f;        // velocidade sendo sugada pelo buraco
constexpr float REVERSE_SPEED = 80.f;

// Tiro
constexpr float SHOT_SPEED = 650.f;
constexpr float SHOT_SPEED_FAST = 1100.f;
constexpr float RELOAD_TIME = 0.16f;
constexpr size_t AMMO_SIZE = 3;           // bolas na fila de munição do sapo
constexpr float MOUTH_DIST = 30.f;         // distância do centro do sapo até a bola na boca

// Poderes
constexpr float REVERSE_TIME = 3.f;
constexpr float SLOW_TIME = 6.f;
constexpr float ACCURACY_TIME = 15.f;
constexpr float POWER_LIFE = 12.f;
constexpr float BOMB_RADIUS = 64.f;

// Posições no mapa
const sf::Vector2f FROG_POS(320.f, 245.f);
const sf::Vector2f HOLE_POS(577.f, 327.f);

// Recortes da folha sapo.png (coordenadas medidas no arquivo)
const sf::IntRect FROG_RECT({1, 1}, {108, 108});
const sf::IntRect LIDS_HALF_RECT({110, 1}, {58, 20});
const sf::IntRect LIDS_CLOSED_RECT({110, 21}, {58, 20});
const sf::IntRect TONGUE_RECT({111, 58}, {29, 38});
const sf::IntRect GOD_RECT({1, 127}, {156, 140});
const sf::Color TONGUE_BG(66, 55, 66);     // fundo do recorte da língua (vira transparente)

// Posições dentro do sapo (em pixels do recorte FROG_RECT, sapo olhando para baixo)
const sf::Vector2f FROG_CENTER_LOCAL(54.f, 54.f);
const sf::Vector2f LIDS_LOCAL(26.f, 40.f);
const sf::Vector2f TONGUE_LOCAL(40.f, 72.f);
const sf::Vector2f NEXT_BALL_LOCAL(55.f, 28.f);

// Brilho (brilho.png: 14 quadros de 20x20)
constexpr int SPARK_FRAMES = 14;
constexpr int SPARK_SIZE = 20;

// ----------------------------------------------------------------------------
//  Utilidades
// ----------------------------------------------------------------------------
float length(sf::Vector2f v) { return std::sqrt(v.x * v.x + v.y * v.y); }

sf::Vector2f normalized(sf::Vector2f v) {
    float len = length(v);
    return len > 0.0001f ? v / len : sf::Vector2f(1.f, 0.f);
}

float dot(sf::Vector2f a, sf::Vector2f b) { return a.x * b.x + a.y * b.y; }

float angleDeg(sf::Vector2f v) { return std::atan2(v.y, v.x) * 180.f / PI; }

sf::Vector2f lerp(sf::Vector2f a, sf::Vector2f b, float t) { return a + (b - a) * t; }

int posMod(int a, int m) { return ((a % m) + m) % m; }

sf::String utf8(const std::string& s) { return sf::String::fromUtf8(s.begin(), s.end()); }

// ----------------------------------------------------------------------------
//  Trilho: polilinha com comprimento de arco acumulado
// ----------------------------------------------------------------------------
class Path {
public:
    bool load(const std::string& file) {
        std::ifstream in(file);
        if (!in) return false;

        std::vector<sf::Vector2f> pts;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            float x, y;
            if (!(ss >> x >> y)) continue;
            if (!pts.empty() && length(sf::Vector2f(x, y) - pts.back()) < 0.01f) continue;
            pts.push_back({x, y});
        }
        if (pts.size() < 2) return false;

        points = pts;
        acc.assign(points.size(), 0.f);
        for (size_t i = 1; i < points.size(); ++i)
            acc[i] = acc[i - 1] + length(points[i] - points[i - 1]);
        startDir = normalized(points[1] - points[0]);
        endDir = normalized(points.back() - points[points.size() - 2]);
        return true;
    }

    float total() const { return acc.back(); }

    // Fora do intervalo [0, total] o trilho é estendido em linha reta.
    sf::Vector2f pointAt(float s) const {
        if (s <= 0.f) return points.front() + startDir * s;
        if (s >= total()) return points.back() + endDir * (s - total());
        size_t i = static_cast<size_t>(std::upper_bound(acc.begin(), acc.end(), s) - acc.begin()) - 1;
        float t = (s - acc[i]) / (acc[i + 1] - acc[i]);
        return lerp(points[i], points[i + 1], t);
    }

    sf::Vector2f tangentAt(float s) const { return normalized(pointAt(s + 3.f) - pointAt(s - 3.f)); }

private:
    std::vector<sf::Vector2f> points;
    std::vector<float> acc;
    sf::Vector2f startDir, endDir;
};

// ----------------------------------------------------------------------------
//  Efeitos sonoros sintetizados (o projeto não tem arquivos de som)
// ----------------------------------------------------------------------------
class Sfx {
public:
    enum Id { Shoot, Click, Pop, Swap, Warn, Lose, Win, Power, Zuma, Bomb, Count };

    void init() {
        const float tau = 2.f * PI;
        // Frequência que varia linearmente de f0 a f1 ao longo de d segundos.
        auto sweep = [tau](float t, float d, float f0, float f1) {
            return std::sin(tau * (f0 * t + (f1 - f0) * t * t / (2.f * d)));
        };
        std::mt19937 noiseRng(1234);
        std::uniform_real_distribution<float> noise(-1.f, 1.f);

        buffers[Shoot] = synth(0.12f, [&](float t) {
            return (0.6f * sweep(t, 0.12f, 520.f, 140.f) + 0.4f * noise(noiseRng)) * std::exp(-t * 35.f);
        });
        buffers[Click] = synth(0.06f, [&](float t) {
            return (0.7f * std::sin(tau * 180.f * t) + 0.3f * noise(noiseRng)) * std::exp(-t * 70.f);
        });
        buffers[Pop] = synth(0.22f, [&](float t) {
            float s = sweep(t, 0.22f, 520.f, 1500.f);
            return (0.7f * s + 0.3f * std::sin(tau * 2.f * 700.f * t)) * std::exp(-t * 14.f);
        });
        buffers[Swap] = synth(0.07f, [&](float t) { return std::sin(tau * 950.f * t) * std::exp(-t * 45.f); });
        buffers[Warn] = synth(0.16f, [&](float t) {
            float sq = std::sin(tau * 330.f * t) > 0.f ? 1.f : -1.f;
            return 0.45f * sq * std::exp(-t * 18.f);
        });
        buffers[Lose] = synth(1.4f, [&](float t) {
            return sweep(t, 1.4f, 520.f, 55.f) * (0.75f + 0.25f * std::sin(tau * 9.f * t)) * std::exp(-t * 1.5f);
        });
        buffers[Win] = synth(0.8f, [&](float t) {
            const float notes[] = {523.f, 659.f, 784.f, 1047.f};
            int i = std::min(3, static_cast<int>(t / 0.16f));
            float lt = t - i * 0.16f;
            return std::sin(tau * notes[i] * t) * std::exp(-lt * 6.f) * (i == 3 ? std::exp(-lt * 2.f) : 1.f);
        });
        buffers[Power] = synth(0.35f, [&](float t) {
            return sweep(t, 0.35f, 400.f, 1700.f) * std::exp(-t * 6.f);
        });
        buffers[Zuma] = synth(1.1f, [&](float t) {
            float c = std::sin(tau * 392.f * t) + std::sin(tau * 494.f * t) + std::sin(tau * 587.f * t);
            return 0.33f * c * std::min(1.f, t * 30.f) * std::exp(-t * 2.5f);
        });
        buffers[Bomb] = synth(0.5f, [&](float t) {
            return (0.5f * noise(noiseRng) + 0.5f * std::sin(tau * 70.f * t)) * std::exp(-t * 7.f);
        });

        pool.reserve(POOL_SIZE);
        for (int i = 0; i < POOL_SIZE; ++i) pool.emplace_back(buffers[Click]);
    }

    void play(Id id, float pitch = 1.f, float volume = 80.f) {
        if (pool.empty()) return;
        sf::Sound& s = pool[nextSlot];
        nextSlot = (nextSlot + 1) % pool.size();
        s.stop();
        s.setBuffer(buffers[id]);
        s.setPitch(pitch);
        s.setVolume(volume);
        s.play();
    }

private:
    static constexpr int POOL_SIZE = 16;

    static sf::SoundBuffer synth(float duration, const std::function<float(float)>& wave) {
        const unsigned rate = 44100;
        const size_t n = static_cast<size_t>(duration * rate);
        std::vector<std::int16_t> samples(n);
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / rate;
            float fade = std::min(1.f, (duration - t) * 200.f);  // evita estalo no final
            float v = std::clamp(wave(t) * fade, -1.f, 1.f);
            samples[i] = static_cast<std::int16_t>(v * 26000.f);
        }
        sf::SoundBuffer buffer;
        if (!buffer.loadFromSamples(samples.data(), n, 1, rate, {sf::SoundChannel::Mono}))
            std::cerr << "Falha ao gerar efeito sonoro\n";
        return buffer;
    }

    std::array<sf::SoundBuffer, Count> buffers;
    std::vector<sf::Sound> pool;
    size_t nextSlot = 0;
};

// ----------------------------------------------------------------------------
//  Entidades
// ----------------------------------------------------------------------------
enum class Power { None, Bomb, Reverse, Slow, Accuracy };

struct ChainBall {
    int color = 0;
    float s = 0.f;              // posição ao longo do trilho
    float size = 1.f;           // 0..1, cresce enquanto a bola é inserida
    float roll = 0.f;           // distância rolada (define o quadro da animação)
    float pull = 0.f;           // velocidade de atração (só no início de um segmento)
    Power power = Power::None;
    float powerTime = 0.f;
    bool inserting = false;
    float insertT = 0.f;
    sf::Vector2f insertFrom;
    bool settled = false;       // acabou de ser inserida: verificar combinação
    bool fromShot = false;
    int pendingCombo = 0;       // nível de combo esperando a corrente fechar o buraco

    // Auxiliares de um passo de simulação (valem só dentro de updateChain/removeBalls).
    float prevS = 0.f;          // posição no início do passo
    bool wasLinked = false;     // estava encostada na bola da frente no início do passo
    bool marked = false;        // marcada para ser removida
};

struct Shot {
    sf::Vector2f pos;
    sf::Vector2f vel;
    int color = 0;
};

struct Pop {
    sf::Vector2f pos;
    int color = 0;
    float angle = 0.f;
    float t = 0.f;
};

struct Spark {
    sf::Vector2f pos;
    float scale = 1.f;
    float delay = 0.f;
    float t = 0.f;
};

struct FloatText {
    std::string text;
    sf::Vector2f pos;
    sf::Color color;
    unsigned size = 14;
    float t = 0.f;
    float life = 1.2f;
};

enum class State { Title, Playing, Paused, Losing, LevelDone, GameOver };

// ----------------------------------------------------------------------------
//  Jogo
// ----------------------------------------------------------------------------
class Game {
public:
    // A corrente é uma lista duplamente encadeada: `primeiro()` é a bola mais atrás
    // (começo do trilho) e `ultimo()` a mais à frente (perto do buraco).
    using BallNode = Lista<ChainBall>::No;

    bool init();
    void run();

private:
    // --- fluxo ---
    void newGame();
    void startLevel();
    void update(float dt);
    void updatePlaying(float dt);
    void updateLosing(float dt);
    void onMousePressed(sf::Mouse::Button button);
    void onKeyPressed(sf::Keyboard::Key key);
    void updateViewport(sf::Vector2u size);

    // --- corrente ---
    static float spacing(const ChainBall& a, const ChainBall& b) { return BALL_D * (a.size + b.size) * 0.5f; }
    // `a` está encostada na bola da frente (a->proximo precisa existir)?
    static bool connected(const BallNode* a) {
        return a->proximo->valor.s - a->valor.s <= spacing(a->valor, a->proximo->valor) + CONNECT_EPS;
    }
    sf::Vector2f ballPos(const ChainBall& b) const;
    float headFraction() const { return balls.vazia() ? 0.f : balls.ultimo()->valor.s / path.total(); }
    float targetSpeed() const;
    void updateChain(float dt);
    void growInserts(float dt);
    void settleInserts();
    void spawnBalls();
    int pickSpawnColor();
    // `before` recebe o nó que ficou logo antes do trecho removido (nullptr se não havia).
    bool tryMatch(BallNode* node, int combo, BallNode*& before);
    void removeBalls(BallNode* first, BallNode* last, int combo, BallNode*& before);
    void activatePower(Power p, sf::Vector2f where);
    void updatePowerUps(float dt);
    void addScore(int points);

    // --- sapo / tiros ---
    sf::Vector2f aimDir() const { return normalized(mouse - FROG_POS); }
    void fire();
    void swapBalls();
    void updateShots(float dt);
    void insertShot(const Shot& shot, BallNode* hit);
    int randomChainColor();
    void refreshAmmo();

    // --- efeitos ---
    void spawnSparks(sf::Vector2f pos, int count, float spread);
    void addText(const std::string& text, sf::Vector2f pos, sf::Color color, unsigned size, float life = 1.2f);
    void updateEffects(float dt);

    // --- desenho ---
    void render();
    void drawBall(int color, sf::Vector2f pos, float dirDeg, int frame, float scale = 1.f,
                  sf::Color tint = sf::Color::White, const sf::RenderStates& states = sf::RenderStates::Default);
    void drawChain();
    void drawAmmoQueue();
    void drawPowerIcon(const ChainBall& b, sf::Vector2f pos);
    void drawAimLine();
    void drawFrog();
    void drawEffects();
    void drawHud();
    void drawOverlay();
    void drawText(const std::string& str, sf::Vector2f pos, unsigned size, sf::Color color,
                  float alignX = 0.5f, float outline = 2.f, sf::Color outlineColor = sf::Color(20, 12, 5));
    void drawDarken(std::uint8_t alpha);

    int randInt(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
    float randF(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

    // --- recursos ---
    sf::RenderWindow window;
    sf::View view;
    sf::Texture texMap, texBalls, texFrog, texSpark;
    sf::Font font;
    Path path;
    Sfx sfx;
    std::mt19937 rng{std::random_device{}()};

    // --- estado ---
    State state = State::Title;
    float stateTime = 0.f;
    float clockTime = 0.f;
    int level = 1;
    int lives = 3;
    long score = 0;
    long levelStartScore = 0;
    long nextLifeScore = 50000;
    long target = 1000;
    int numColors = 4;
    float baseSpeed = 20.f;

    Lista<ChainBall> balls;
    std::vector<Shot> shots;
    std::vector<Pop> pops;
    std::vector<Spark> sparks;
    std::vector<FloatText> texts;

    bool spawning = true;
    bool rushing = true;
    float curSpeed = 0.f;
    float slowT = 0.f, reverseT = 0.f, accuracyT = 0.f;
    float powerSpawnT = 8.f;
    float warnT = 0.f;
    int shotStreak = 0;

    // sapo
    sf::Vector2f mouse{FROG_POS.x, FROG_POS.y + 100.f};
    Fila<int> ammo;           // cores das próximas bolas; a da frente está na boca
    float reload = 0.f;       // >0: bola nova ainda subindo para a boca
    float blinkIn = 3.f;      // tempo até a próxima piscada
    float blinkT = -1.f;      // >=0: piscando
};

// ============================================================================
//  Inicialização e laço principal
// ============================================================================
bool Game::init() {
    bool ok = true;
    ok &= texMap.loadFromFile("sprites/mapa.png");
    ok &= texBalls.loadFromFile("sprites/balls.png");
    ok &= texSpark.loadFromFile("sprites/brilho.png");

    // A língua vem sobre um fundo roxo sólido; deixamos esse fundo transparente.
    sf::Image frogImg;
    if (frogImg.loadFromFile("sprites/sapo.png")) {
        for (int y = TONGUE_RECT.position.y; y < TONGUE_RECT.position.y + TONGUE_RECT.size.y; ++y)
            for (int x = TONGUE_RECT.position.x; x < TONGUE_RECT.position.x + TONGUE_RECT.size.x; ++x) {
                sf::Vector2u p(static_cast<unsigned>(x), static_cast<unsigned>(y));
                sf::Color c = frogImg.getPixel(p);
                if (c.r == TONGUE_BG.r && c.g == TONGUE_BG.g && c.b == TONGUE_BG.b)
                    frogImg.setPixel(p, sf::Color::Transparent);
            }
        ok &= texFrog.loadFromImage(frogImg);
    } else {
        ok = false;
    }
    ok &= font.openFromFile("fontes/DejaVuSans-Bold.ttf");
    if (!path.load("sprites/caminho.txt")) {
        std::cerr << "Nao foi possivel ler sprites/caminho.txt\n";
        ok = false;
    }
    if (!ok) {
        std::cerr << "Erro ao carregar recursos. Rode o jogo a partir da pasta do projeto.\n";
        return false;
    }
    texBalls.setSmooth(true);
    texFrog.setSmooth(true);
    texSpark.setSmooth(true);
    texMap.setSmooth(true);

    sfx.init();

    window.create(sf::VideoMode({WINDOW_W, WINDOW_H}), "Zuma");
    window.setVerticalSyncEnabled(true);
    view = sf::View(sf::FloatRect({0.f, 0.f}, {SCREEN_W, SCREEN_H}));
    updateViewport(window.getSize());
    return true;
}

void Game::updateViewport(sf::Vector2u size) {
    // Mantém a proporção 4:3 com faixas pretas (letterbox).
    float wr = static_cast<float>(size.x) / SCREEN_W;
    float hr = static_cast<float>(size.y) / SCREEN_H;
    sf::FloatRect vp({0.f, 0.f}, {1.f, 1.f});
    if (wr > hr) {
        vp.size.x = hr / wr;
        vp.position.x = (1.f - vp.size.x) / 2.f;
    } else {
        vp.size.y = wr / hr;
        vp.position.y = (1.f - vp.size.y) / 2.f;
    }
    view.setViewport(vp);
}

void Game::run() {
    sf::Clock clock;
    float accumulator = 0.f;

    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                updateViewport(resized->size);
            } else if (event->is<sf::Event::FocusLost>()) {
                if (state == State::Playing) state = State::Paused;
            } else if (const auto* mb = event->getIf<sf::Event::MouseButtonPressed>()) {
                mouse = window.mapPixelToCoords(mb->position, view);
                onMousePressed(mb->button);
            } else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                onKeyPressed(key->code);
            }
        }
        if (!window.isOpen()) break;

        mouse = window.mapPixelToCoords(sf::Mouse::getPosition(window), view);

        accumulator += std::min(clock.restart().asSeconds(), 0.25f);
        while (accumulator >= FIXED_DT) {
            update(FIXED_DT);
            accumulator -= FIXED_DT;
        }
        render();
    }
}

void Game::onMousePressed(sf::Mouse::Button button) {
    switch (state) {
    case State::Title:
        if (button == sf::Mouse::Button::Left) newGame();
        break;
    case State::Playing:
        if (button == sf::Mouse::Button::Left) fire();
        else if (button == sf::Mouse::Button::Right) swapBalls();
        break;
    case State::Paused:
        if (button == sf::Mouse::Button::Left) state = State::Playing;
        break;
    case State::LevelDone:
        if (button == sf::Mouse::Button::Left && stateTime > 1.f) {
            ++level;
            startLevel();
        }
        break;
    case State::GameOver:
        if (button == sf::Mouse::Button::Left && stateTime > 1.f) {
            state = State::Title;
            stateTime = 0.f;
        }
        break;
    case State::Losing:
        break;
    }
}

void Game::onKeyPressed(sf::Keyboard::Key key) {
    if (key == sf::Keyboard::Key::P || key == sf::Keyboard::Key::Escape) {
        if (state == State::Playing) state = State::Paused;
        else if (state == State::Paused) state = State::Playing;
    } else if (key == sf::Keyboard::Key::Space && state == State::Playing) {
        swapBalls();
    }
}

void Game::newGame() {
    level = 1;
    lives = 3;
    score = 0;
    nextLifeScore = 50000;
    startLevel();
}

void Game::startLevel() {
    // Dificuldade: mais cores, corrente mais rápida e meta maior a cada fase.
    numColors = level <= 2 ? 4 : (level <= 4 ? 5 : 6);
    baseSpeed = std::min(20.f + 2.5f * static_cast<float>(level - 1), 42.f);
    target = 1000 + 400L * (level - 1);

    balls.limpar();
    shots.clear();
    pops.clear();
    sparks.clear();
    texts.clear();
    levelStartScore = score;
    spawning = true;
    rushing = true;
    curSpeed = RUSH_SPEED;
    slowT = reverseT = accuracyT = 0.f;
    powerSpawnT = randF(6.f, 12.f);
    warnT = 0.f;
    shotStreak = 0;
    reload = 0.f;
    ammo.limpar();
    for (size_t i = 0; i < AMMO_SIZE; ++i) ammo.enfileirar(randInt(0, numColors - 1));
    state = State::Playing;
    stateTime = 0.f;
}

// ============================================================================
//  Atualização
// ============================================================================
void Game::update(float dt) {
    clockTime += dt;
    stateTime += dt;

    // O sapo pisca de vez em quando (em qualquer tela).
    if (blinkT >= 0.f) {
        blinkT += dt;
        if (blinkT > 0.22f) blinkT = -1.f;
    } else if ((blinkIn -= dt) <= 0.f) {
        blinkT = 0.f;
        blinkIn = randF(2.f, 6.f);
    }

    switch (state) {
    case State::Playing: updatePlaying(dt); break;
    case State::Losing: updateLosing(dt); break;
    case State::LevelDone:
    case State::GameOver: updateEffects(dt); break;
    default: break;
    }
}

void Game::updatePlaying(float dt) {
    slowT = std::max(0.f, slowT - dt);
    reverseT = std::max(0.f, reverseT - dt);
    accuracyT = std::max(0.f, accuracyT - dt);
    reload = std::max(0.f, reload - dt);

    updateShots(dt);
    growInserts(dt);
    updateChain(dt);
    settleInserts();
    spawnBalls();
    updatePowerUps(dt);
    refreshAmmo();
    updateEffects(dt);

    // Barra do Zuma cheia: param de sair bolas novas.
    if (spawning && score - levelStartScore >= target) {
        spawning = false;
        sfx.play(Sfx::Zuma);
        addText("ZUMA!", {SCREEN_W / 2.f, SCREEN_H / 2.f - 40.f}, sf::Color(255, 215, 60), 48, 2.2f);
    }

    // Alerta de perigo quando a frente da corrente se aproxima do buraco.
    float head = headFraction();
    if (head > 0.8f) {
        warnT -= dt;
        if (warnT <= 0.f) {
            sfx.play(Sfx::Warn, 1.f, 45.f);
            warnT = 0.2f + 0.8f * (1.f - (head - 0.8f) / 0.2f);
        }
    }

    // Derrota: a primeira bola chegou ao buraco.
    if (!balls.vazia() && balls.ultimo()->valor.s >= path.total()) {
        state = State::Losing;
        stateTime = 0.f;
        shots.clear();
        sfx.play(Sfx::Lose);
        return;
    }

    // Vitória: barra cheia e corrente destruída.
    if (!spawning && balls.vazia()) {
        state = State::LevelDone;
        stateTime = 0.f;
        shots.clear();
        sfx.play(Sfx::Win);
    }
}

void Game::updateLosing(float dt) {
    for (ChainBall& b : balls) {
        b.s += LOSE_SPEED * dt;
        b.roll += LOSE_SPEED * dt;
        b.size = 1.f;
        b.inserting = false;
    }
    for (BallNode* n = balls.primeiro(); n && n->proximo; n = n->proximo)
        n->proximo->valor.s = std::max(n->proximo->valor.s, n->valor.s + BALL_D);
    while (!balls.vazia() && balls.ultimo()->valor.s >= path.total() + BALL_R)
        balls.removerFim();
    updateEffects(dt);

    if (balls.vazia() && stateTime > 1.5f) {
        --lives;
        if (lives <= 0) {
            state = State::GameOver;
            stateTime = 0.f;
        } else {
            score = levelStartScore;  // como no original, a fase recomeça do zero
            startLevel();
        }
    }
}

// ----------------------------------------------------------------------------
//  Corrente
// ----------------------------------------------------------------------------
sf::Vector2f Game::ballPos(const ChainBall& b) const {
    sf::Vector2f p = path.pointAt(b.s);
    if (!b.inserting) return p;
    float t = std::clamp(b.insertT, 0.f, 1.f);
    t = t * t * (3.f - 2.f * t);
    return lerp(b.insertFrom, p, t);
}

float Game::targetSpeed() const {
    if (reverseT > 0.f) return -REVERSE_SPEED;
    if (rushing) return RUSH_SPEED;
    float v = baseSpeed;
    // Perto do buraco a corrente desacelera um pouco, dando chance ao jogador.
    float head = headFraction();
    if (head > 0.75f) v *= 1.f - 0.45f * std::min(1.f, (head - 0.75f) / 0.25f);
    if (slowT > 0.f) v *= 0.35f;
    return v;
}

void Game::updateChain(float dt) {
    if (balls.vazia()) return;

    if (rushing && headFraction() >= RUSH_FRACTION) rushing = false;
    float wanted = targetSpeed();
    float blend = std::min(1.f, dt * (rushing ? 20.f : 3.f));
    curSpeed += (wanted - curSpeed) * blend;

    // Guarda a situação no início do passo (quem estava encostado em quem).
    for (BallNode* n = balls.primeiro(); n; n = n->proximo) {
        n->valor.prevS = n->valor.s;
        n->valor.wasLinked = n->proximo && connected(n);
    }

    // 1) O segmento de trás é empurrado pela "máquina" no início do trilho.
    BallNode* end = balls.primeiro();
    for (;;) {
        end->valor.s += curSpeed * dt;
        if (!end->valor.wasLinked) break;
        end = end->proximo;
    }

    // 2) Segmentos da frente ficam parados, exceto se a cor da ponta combina
    //    com a do segmento de trás: nesse caso são atraídos para trás.
    for (BallNode* start = end->proximo; start;) {
        BallNode* segEnd = start;
        while (segEnd->valor.wasLinked) segEnd = segEnd->proximo;
        ChainBall& first = start->valor;
        if (start->anterior->valor.color == first.color) {
            first.pull = std::min(PULL_MAX, first.pull + PULL_ACC * dt);
            for (BallNode* n = start;; n = n->proximo) {
                n->valor.s -= first.pull * dt;
                if (n == segEnd) break;
            }
        } else {
            first.pull = 0.f;
        }
        start = segEnd->proximo;
    }

    // 3) Bolas não se sobrepõem: quem encosta empurra a da frente.
    for (BallNode* n = balls.primeiro(); n->proximo; n = n->proximo) {
        float need = n->valor.s + spacing(n->valor, n->proximo->valor);
        if (n->proximo->valor.s < need) n->proximo->valor.s = need;
    }

    for (ChainBall& b : balls) b.roll += b.s - b.prevS;

    // 4) Segmentos que se encostaram agora podem formar combinações (combos).
    //    Percorre da frente para trás; uma remoção só afeta nós à frente de `before`,
    //    por onde o percurso já passou.
    BallNode* cur = balls.ultimo();
    while (cur && cur->anterior) {
        BallNode* prev = cur->anterior;
        if (prev->valor.wasLinked || !connected(prev)) {
            cur = prev;
            continue;
        }

        ChainBall& front = cur->valor;
        int pending = front.pendingCombo;
        front.pendingCombo = 0;
        bool wasPulled = front.pull > 0.f;
        front.pull = 0.f;
        if (wasPulled) sfx.play(Sfx::Click, 1.f, 70.f);

        if (prev->valor.color == front.color && !prev->valor.inserting && !front.inserting) {
            BallNode* before = nullptr;
            int combo = pending > 0 ? pending + 1 : 1;
            if (tryMatch(prev, combo, before)) {
                cur = before;
                continue;
            }
        }
        cur = prev;
    }
}

void Game::growInserts(float dt) {
    // Roda antes de updateChain, para que o espaço aberto já seja respeitado neste passo.
    for (ChainBall& b : balls) {
        if (!b.inserting) continue;
        b.insertT += dt / INSERT_TIME;
        b.size = std::min(1.f, b.insertT);
        if (b.insertT >= 1.f) {
            b.inserting = false;
            b.size = 1.f;
            b.settled = true;
        }
    }
}

void Game::settleInserts() {
    // Verifica combinações das bolas recém-encaixadas, uma de cada vez
    // (cada remoção apaga nós da lista).
    for (;;) {
        BallNode* node = balls.primeiro();
        while (node && !node->valor.settled) node = node->proximo;
        if (!node) break;
        node->valor.settled = false;
        bool fromShot = node->valor.fromShot;
        node->valor.fromShot = false;

        BallNode* before = nullptr;
        bool matched = tryMatch(node, 1, before);
        if (!fromShot) continue;
        if (matched) {
            ++shotStreak;
            if (shotStreak >= 5) {
                int bonus = 50 * shotStreak;
                addScore(bonus);
                addText("SEQUÊNCIA x" + std::to_string(shotStreak) + " +" + std::to_string(bonus),
                        {SCREEN_W / 2.f, 90.f}, sf::Color(120, 220, 255), 16, 1.5f);
            }
        } else {
            shotStreak = 0;
            sfx.play(Sfx::Click, 1.1f, 60.f);
        }
    }
}

int Game::pickSpawnColor() {
    if (balls.vazia() || numColors < 2) return randInt(0, numColors - 1);
    BallNode* tail = balls.primeiro();
    int c0 = tail->valor.color;
    bool pairAlready = tail->proximo && tail->proximo->valor.color == c0 && connected(tail);
    // Nunca nascem 3 iguais seguidas (senão ficariam "presas" sem estourar).
    if (!pairAlready && randF(0.f, 1.f) < 0.4f) return c0;
    int c = randInt(0, numColors - 2);
    return c >= c0 ? c + 1 : c;
}

void Game::spawnBalls() {
    if (!spawning) return;
    // Bolas novas saem do começo do trilho (fora da tela) enquanto houver espaço.
    while (balls.vazia() || balls.primeiro()->valor.s > 0.f) {
        ChainBall b;
        b.color = pickSpawnColor();
        if (balls.vazia()) {
            b.s = 0.f;
        } else {
            const ChainBall& tail = balls.primeiro()->valor;
            b.s = std::min(tail.s - BALL_D * (1.f + tail.size) * 0.5f, 0.f);
        }
        balls.inserirInicio(b);
        if (b.s <= 0.f) break;
    }
}

bool Game::tryMatch(BallNode* node, int combo, BallNode*& before) {
    const int c = node->valor.color;
    BallNode* a = node;
    BallNode* b = node;
    size_t count = 1;
    while (a->anterior && a->anterior->valor.color == c && !a->anterior->valor.inserting && connected(a->anterior)) {
        a = a->anterior;
        ++count;
    }
    while (b->proximo && b->proximo->valor.color == c && !b->proximo->valor.inserting && connected(b)) {
        b = b->proximo;
        ++count;
    }
    if (count < 3) return false;

    removeBalls(a, b, combo, before);
    return true;
}

void Game::removeBalls(BallNode* first, BallNode* last, int combo, BallNode*& before) {
    const int runColor = first->valor.color;

    // Marca o trecho combinado. Bombas explodem tudo ao redor (e podem detonar
    // outras bombas): a pilha guarda as bolas marcadas cujo poder ainda não foi tratado.
    Pilha<BallNode*> pending;
    for (BallNode* n = first;; n = n->proximo) {
        n->valor.marked = true;
        pending.empilhar(n);
        if (n == last) break;
    }
    while (!pending.vazia()) {
        BallNode* q = pending.desempilhar();
        if (q->valor.power != Power::Bomb) continue;
        sf::Vector2f center = ballPos(q->valor);
        spawnSparks(center, 14, BOMB_RADIUS * 0.8f);
        sfx.play(Sfx::Bomb);
        for (BallNode* n = balls.primeiro(); n; n = n->proximo) {
            if (n->valor.marked || n->valor.s < 0.f) continue;
            if (length(ballPos(n->valor) - center) < BOMB_RADIUS) {
                n->valor.marked = true;
                pending.empilhar(n);
            }
        }
    }

    // Efeitos, poderes e pontuação.
    int count = 0;
    sf::Vector2f center;
    before = nullptr;
    bool seenFirst = false;
    for (BallNode* n = balls.primeiro(); n; n = n->proximo) {
        if (!n->valor.marked) continue;
        if (!seenFirst) {
            before = n->anterior;  // não está marcado: é o primeiro marcado
            seenFirst = true;
        }
        ++count;
        sf::Vector2f p = ballPos(n->valor);
        center += p;
        pops.push_back({p, n->valor.color, angleDeg(path.tangentAt(n->valor.s)), 0.f});
        spawnSparks(p, 1, 4.f);
        if (n->valor.power != Power::None && n->valor.power != Power::Bomb) activatePower(n->valor.power, p);
    }
    if (count == 0) return;
    center /= static_cast<float>(count);

    int points = 10 * count + (combo > 1 ? 100 * (combo - 1) : 0);
    addScore(points);
    sf::Color color = BALL_TINT[static_cast<size_t>(runColor)];
    addText("+" + std::to_string(points), center, color, 16);
    if (combo > 1) addText("COMBO x" + std::to_string(combo), center + sf::Vector2f(0.f, -20.f), sf::Color::White, 18, 1.5f);
    sfx.play(Sfx::Pop, 1.f + 0.12f * static_cast<float>(combo - 1));

    // Remove cada trecho marcado e guarda a bola que veio logo depois dele
    // (a ponta da frente do buraco aberto na corrente).
    std::vector<BallNode*> gapFronts;
    for (BallNode* n = balls.primeiro(); n;) {
        if (!n->valor.marked) {
            n = n->proximo;
            continue;
        }
        BallNode* runEnd = n;
        while (runEnd->proximo && runEnd->proximo->valor.marked) runEnd = runEnd->proximo;
        BallNode* stop = runEnd->proximo;
        gapFronts.push_back(stop);
        while (n != stop) {
            BallNode* next = n->proximo;
            balls.remover(n);
            n = next;
        }
    }

    // Se as pontas do buraco têm a mesma cor, a parte da frente será atraída
    // de volta e, ao encostar, pode continuar o combo.
    for (BallNode* f : gapFronts) {
        if (!f || !f->anterior) continue;
        if (f->anterior->valor.color == f->valor.color)
            f->valor.pendingCombo = std::max(f->valor.pendingCombo, combo);
    }
}

void Game::activatePower(Power p, sf::Vector2f where) {
    sfx.play(Sfx::Power);
    switch (p) {
    case Power::Reverse:
        reverseT = REVERSE_TIME;
        addText("RÉ!", where + sf::Vector2f(0.f, -36.f), sf::Color(255, 160, 60), 18);
        break;
    case Power::Slow:
        slowT = SLOW_TIME;
        addText("DEVAGAR!", where + sf::Vector2f(0.f, -36.f), sf::Color(120, 200, 255), 18);
        break;
    case Power::Accuracy:
        accuracyT = ACCURACY_TIME;
        addText("PRECISÃO!", where + sf::Vector2f(0.f, -36.f), sf::Color(140, 255, 140), 18);
        break;
    default: break;
    }
}

void Game::updatePowerUps(float dt) {
    for (ChainBall& b : balls) {
        if (b.power == Power::None) continue;
        b.powerTime -= dt;
        if (b.powerTime <= 0.f) b.power = Power::None;
    }

    powerSpawnT -= dt;
    if (powerSpawnT > 0.f) return;
    powerSpawnT = randF(8.f, 16.f);

    std::vector<ChainBall*> candidates;
    for (ChainBall& c : balls) {
        float f = c.s / path.total();
        if (f > 0.05f && f < 0.85f && c.power == Power::None && !c.inserting) candidates.push_back(&c);
    }
    if (candidates.empty()) return;
    ChainBall& b = *candidates[static_cast<size_t>(randInt(0, static_cast<int>(candidates.size()) - 1))];
    b.power = static_cast<Power>(randInt(1, 4));
    b.powerTime = POWER_LIFE;
}

void Game::addScore(int points) {
    score += points;
    if (score >= nextLifeScore) {
        nextLifeScore += 50000;
        ++lives;
        addText("VIDA EXTRA!", {SCREEN_W / 2.f, 120.f}, sf::Color(120, 255, 120), 20, 2.f);
    }
}

// ----------------------------------------------------------------------------
//  Sapo e tiros
// ----------------------------------------------------------------------------
int Game::randomChainColor() {
    std::array<bool, COLOR_COUNT> present{};
    int count = 0;
    for (const ChainBall& b : balls)
        if (!present[static_cast<size_t>(b.color)]) {
            present[static_cast<size_t>(b.color)] = true;
            ++count;
        }
    if (count == 0) return randInt(0, numColors - 1);
    int pick = randInt(0, count - 1);
    for (int c = 0; c < COLOR_COUNT; ++c)
        if (present[static_cast<size_t>(c)] && pick-- == 0) return c;
    return 0;
}

void Game::refreshAmmo() {
    // Como no original: o sapo só carrega cores que ainda existem na corrente.
    if (balls.vazia()) return;
    std::array<bool, COLOR_COUNT> present{};
    for (const ChainBall& b : balls) present[static_cast<size_t>(b.color)] = true;
    for (size_t i = 0; i < ammo.tamanho(); ++i) {
        int& color = ammo.em(i);
        if (!present[static_cast<size_t>(color)]) color = randomChainColor();
    }
}

void Game::fire() {
    if (reload > 0.f) return;
    sf::Vector2f dir = aimDir();
    float speed = accuracyT > 0.f ? SHOT_SPEED_FAST : SHOT_SPEED;
    // Atirar = sair da frente da fila; a bola nova entra no fim.
    shots.push_back({FROG_POS + dir * MOUTH_DIST, dir * speed, ammo.desenfileirar()});
    ammo.enfileirar(randomChainColor());
    reload = RELOAD_TIME;
    sfx.play(Sfx::Shoot, 1.f, 70.f);
}

void Game::swapBalls() {
    // Trocar = a bola da boca vai para o fim da fila e a próxima sobe.
    bool allEqual = true;
    for (size_t i = 1; i < ammo.tamanho(); ++i) allEqual &= ammo.em(i) == ammo.frente();
    if (allEqual) return;
    ammo.enfileirar(ammo.desenfileirar());
    sfx.play(Sfx::Swap, 1.f, 60.f);
}

void Game::updateShots(float dt) {
    for (size_t i = 0; i < shots.size();) {
        Shot& shot = shots[i];
        shot.pos += shot.vel * dt;

        bool gone = shot.pos.x < -BALL_D || shot.pos.x > SCREEN_W + BALL_D ||
                    shot.pos.y < -BALL_D || shot.pos.y > SCREEN_H + BALL_D;
        if (gone) {
            shots.erase(shots.begin() + static_cast<long>(i));
            continue;
        }

        BallNode* hit = nullptr;
        float best = HIT_DIST;
        for (BallNode* n = balls.primeiro(); n; n = n->proximo) {
            if (n->valor.s < 0.f) continue;  // ainda escondida no começo do trilho
            float d = length(ballPos(n->valor) - shot.pos);
            if (d < best) {
                best = d;
                hit = n;
            }
        }
        if (hit) {
            Shot s = shot;
            shots.erase(shots.begin() + static_cast<long>(i));
            insertShot(s, hit);
            continue;
        }
        ++i;
    }
}

void Game::insertShot(const Shot& shot, BallNode* hit) {
    // Entra na frente ou atrás da bola atingida, dependendo do lado do impacto.
    const ChainBall& hitBall = hit->valor;
    sf::Vector2f tangent = path.tangentAt(hitBall.s);
    bool after = dot(shot.pos - ballPos(hitBall), tangent) > 0.f;

    ChainBall b;
    b.color = shot.color;
    b.size = 0.f;
    b.s = hitBall.s + (after ? 1.f : -1.f) * BALL_D * hitBall.size * 0.5f;
    b.roll = hitBall.roll;
    b.inserting = true;
    b.insertT = 0.f;
    b.insertFrom = shot.pos;
    b.fromShot = true;

    if (after) balls.inserirDepois(hit, b);
    else balls.inserirAntes(hit, b);
    sfx.play(Sfx::Click, 1.3f, 55.f);
}

// ----------------------------------------------------------------------------
//  Efeitos
// ----------------------------------------------------------------------------
void Game::spawnSparks(sf::Vector2f pos, int count, float spread) {
    for (int i = 0; i < count; ++i) {
        float a = randF(0.f, 2.f * PI);
        float r = randF(0.f, spread);
        sparks.push_back({pos + sf::Vector2f(std::cos(a), std::sin(a)) * r, randF(1.f, 1.8f), randF(0.f, 0.15f), 0.f});
    }
}

void Game::addText(const std::string& text, sf::Vector2f pos, sf::Color color, unsigned size, float life) {
    texts.push_back({text, pos, color, size, 0.f, life});
}

void Game::updateEffects(float dt) {
    for (Pop& p : pops) p.t += dt;
    for (Spark& s : sparks) s.t += dt;
    for (FloatText& t : texts) t.t += dt;
    pops.erase(std::remove_if(pops.begin(), pops.end(), [](const Pop& p) { return p.t > 0.3f; }), pops.end());
    sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.t > s.delay + 0.5f; }),
                 sparks.end());
    texts.erase(std::remove_if(texts.begin(), texts.end(), [](const FloatText& t) { return t.t > t.life; }),
                texts.end());
}

// ============================================================================
//  Desenho
// ============================================================================
void Game::render() {
    window.clear(sf::Color::Black);
    window.setView(view);

    window.draw(sf::Sprite(texMap));

    if (state != State::Title) {
        // Buraco brilha em vermelho quando a corrente se aproxima.
        float head = headFraction();
        if (head > 0.75f && (state == State::Playing || state == State::Losing)) {
            float k = std::min(1.f, (head - 0.75f) / 0.25f);
            float pulse = 0.5f + 0.5f * std::sin(clockTime * (6.f + 10.f * k));
            sf::CircleShape glow(38.f, 40);
            glow.setOrigin({38.f, 38.f});
            glow.setPosition(HOLE_POS);
            glow.setFillColor(sf::Color(255, 30, 20, static_cast<std::uint8_t>(40 + 110 * k * pulse)));
            window.draw(glow, sf::BlendAdd);
        }

        drawChain();
        if (state == State::Playing && accuracyT > 0.f) drawAimLine();
        for (const Shot& s : shots) drawBall(s.color, s.pos, angleDeg(s.vel), 0);
        drawFrog();
        drawEffects();
        drawHud();
        if (state == State::Playing || state == State::Paused) drawAmmoQueue();
    }
    drawOverlay();
    window.display();
}

void Game::drawBall(int color, sf::Vector2f pos, float dirDeg, int frame, float scale, sf::Color tint,
                    const sf::RenderStates& states) {
    sf::Sprite sprite(texBalls);
    sprite.setTextureRect(sf::IntRect({color * BALL_FRAME, frame * BALL_FRAME}, {BALL_FRAME, BALL_FRAME}));
    sprite.setOrigin({BALL_R, BALL_R});
    sprite.setPosition(pos);
    // Nos quadros da folha a bola rola "para baixo"; alinhamos isso à direção do movimento.
    sprite.setRotation(sf::degrees(dirDeg - 90.f));
    sprite.setScale({scale, scale});
    sprite.setColor(tint);
    window.draw(sprite, states);
}

void Game::drawChain() {
    for (const ChainBall& b : balls) {
        if (b.s < -BALL_R) continue;
        sf::Vector2f pos = ballPos(b);
        int frames = BALL_FRAMES[static_cast<size_t>(b.color)];
        float pxPerFrame = PI * BALL_D / static_cast<float>(frames);
        int frame = posMod(static_cast<int>(std::floor(b.roll / pxPerFrame)), frames);
        float scale = b.inserting ? 0.85f + 0.15f * b.size : 1.f;
        drawBall(b.color, pos, angleDeg(path.tangentAt(b.s)), frame, scale);
        if (b.power != Power::None) drawPowerIcon(b, pos);
    }
}

void Game::drawPowerIcon(const ChainBall& b, sf::Vector2f pos) {
    // Pisca mais rápido quando o poder está para sumir.
    float rate = b.powerTime < 3.f ? 14.f : 5.f;
    float pulse = 0.5f + 0.5f * std::sin(clockTime * rate);

    sf::CircleShape glow(BALL_R + 2.f, 32);
    glow.setOrigin({BALL_R + 2.f, BALL_R + 2.f});
    glow.setPosition(pos);
    glow.setFillColor(sf::Color(255, 255, 220, static_cast<std::uint8_t>(40 + 90 * pulse)));
    window.draw(glow, sf::BlendAdd);

    const sf::Color ink(255, 255, 255, 235);
    const sf::Color shade(0, 0, 0, 170);
    sf::CircleShape back(8.5f, 24);
    back.setOrigin({8.5f, 8.5f});
    back.setPosition(pos);
    back.setFillColor(shade);
    window.draw(back);

    switch (b.power) {
    case Power::Bomb: {
        sf::CircleShape bomb(5.f, 20);
        bomb.setOrigin({5.f, 5.f});
        bomb.setPosition(pos + sf::Vector2f(0.f, 1.5f));
        bomb.setFillColor(sf::Color(30, 30, 30));
        bomb.setOutlineThickness(1.2f);
        bomb.setOutlineColor(ink);
        window.draw(bomb);
        sf::RectangleShape fuse({2.f, 4.f});
        fuse.setOrigin({1.f, 4.f});
        fuse.setPosition(pos + sf::Vector2f(2.5f, -3.f));
        fuse.setRotation(sf::degrees(35.f));
        fuse.setFillColor(sf::Color(255, 180, 60));
        window.draw(fuse);
        break;
    }
    case Power::Reverse: {
        // Seta apontando para trás no trilho.
        sf::Vector2f t = path.tangentAt(b.s);
        sf::ConvexShape arrow(3);
        arrow.setPoint(0, {-6.f, 0.f});
        arrow.setPoint(1, {2.f, -5.f});
        arrow.setPoint(2, {2.f, 5.f});
        arrow.setFillColor(ink);
        arrow.setPosition(pos);
        arrow.setRotation(sf::degrees(angleDeg(t)));
        window.draw(arrow);
        sf::RectangleShape tail({5.f, 3.f});
        tail.setOrigin({0.f, 1.5f});
        tail.setPosition(pos);
        tail.setRotation(sf::degrees(angleDeg(t)));
        tail.setFillColor(ink);
        window.draw(tail);
        break;
    }
    case Power::Slow: {
        // Ampulheta.
        sf::ConvexShape top(3), bottom(3);
        top.setPoint(0, {-5.f, -6.f});
        top.setPoint(1, {5.f, -6.f});
        top.setPoint(2, {0.f, 0.f});
        bottom.setPoint(0, {0.f, 0.f});
        bottom.setPoint(1, {5.f, 6.f});
        bottom.setPoint(2, {-5.f, 6.f});
        top.setPosition(pos);
        bottom.setPosition(pos);
        top.setFillColor(ink);
        bottom.setFillColor(ink);
        window.draw(top);
        window.draw(bottom);
        break;
    }
    case Power::Accuracy: {
        // Mira.
        sf::CircleShape ring(4.5f, 20);
        ring.setOrigin({4.5f, 4.5f});
        ring.setPosition(pos);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(1.3f);
        ring.setOutlineColor(ink);
        window.draw(ring);
        sf::RectangleShape h({14.f, 1.4f}), v({1.4f, 14.f});
        h.setOrigin({7.f, 0.7f});
        v.setOrigin({0.7f, 7.f});
        h.setPosition(pos);
        v.setPosition(pos);
        h.setFillColor(ink);
        v.setFillColor(ink);
        window.draw(h);
        window.draw(v);
        break;
    }
    default: break;
    }
}

void Game::drawAmmoQueue() {
    // Mostra a fila de munição: a bola da frente (próxima a ser atirada) vem destacada.
    drawText("FILA", {10.f, SCREEN_H - 50.f}, 10, sf::Color(240, 220, 150), 0.f, 1.5f);
    for (size_t i = 0; i < ammo.tamanho(); ++i) {
        sf::Vector2f pos(24.f + 28.f * static_cast<float>(i), SCREEN_H - 26.f);
        if (i == 0) {
            sf::CircleShape ring(13.f, 24);
            ring.setOrigin({13.f, 13.f});
            ring.setPosition(pos);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(1.5f);
            ring.setOutlineColor(sf::Color(255, 220, 90));
            window.draw(ring);
        }
        drawBall(ammo.em(i), pos, 90.f, 0, i == 0 ? 0.65f : 0.5f);
    }
    drawText("CORRENTE: " + std::to_string(balls.tamanho()) + " bolas", {SCREEN_W - 8.f, SCREEN_H - 12.f}, 10,
             sf::Color(240, 220, 150), 1.f, 1.5f);
}

void Game::drawAimLine() {
    // Poder de precisão: mostra por onde a bola vai passar até bater na corrente.
    sf::Vector2f dir = aimDir();
    sf::Vector2f p = FROG_POS + dir * MOUTH_DIST;
    sf::Color c = BALL_TINT[static_cast<size_t>(ammo.frente())];
    sf::CircleShape dotShape(1.6f, 8);
    dotShape.setOrigin({1.6f, 1.6f});
    for (int step = 0; step < 200; ++step) {
        p += dir * 4.f;
        if (p.x < 0.f || p.x > SCREEN_W || p.y < 0.f || p.y > SCREEN_H) break;
        bool hit = false;
        for (const ChainBall& b : balls)
            if (b.s >= 0.f && length(ballPos(b) - p) < HIT_DIST) {
                hit = true;
                break;
            }
        if (hit) break;
        if (step % 3 == 0) {
            dotShape.setPosition(p);
            dotShape.setFillColor(sf::Color(c.r, c.g, c.b, 200));
            window.draw(dotShape);
        }
    }
}

void Game::drawFrog() {
    sf::Vector2f dir = aimDir();
    // O sprite olha para baixo (+y); giramos para a direção da mira.
    sf::Transform t;
    t.translate(FROG_POS);
    t.rotate(sf::degrees(angleDeg(dir) - 90.f));
    t.translate(-FROG_CENTER_LOCAL);
    sf::RenderStates states;
    states.transform = t;

    sf::Sprite frog(texFrog, FROG_RECT);
    window.draw(frog, states);

    // Pálpebras (meio fechada -> fechada -> meio fechada).
    if (blinkT >= 0.f) {
        bool closed = blinkT > 0.06f && blinkT < 0.16f;
        sf::Sprite lids(texFrog, closed ? LIDS_CLOSED_RECT : LIDS_HALF_RECT);
        lids.setPosition(LIDS_LOCAL);
        window.draw(lids, states);
    }

    // Próxima bola, nas costas do sapo.
    if (state == State::Playing || state == State::Paused) {
        sf::Vector2f nextPos = t.transformPoint(NEXT_BALL_LOCAL);
        drawBall(ammo.em(1), nextPos, angleDeg(dir), 0, 0.45f);
    }

    // Língua + bola na boca. Depois de atirar, a língua recolhe e a nova bola sobe.
    float k = 1.f - reload / RELOAD_TIME;  // 0 = acabou de atirar, 1 = pronta
    float ext = 0.55f + 0.45f * k;
    sf::Sprite tongue(texFrog, TONGUE_RECT);
    tongue.setPosition(TONGUE_LOCAL);
    tongue.setScale({1.f, ext});
    window.draw(tongue, states);

    if (state == State::Playing || state == State::Paused) {
        float dist = MOUTH_DIST * (0.55f + 0.45f * k);
        drawBall(ammo.frente(), FROG_POS + dir * dist, angleDeg(dir), 0, 0.6f + 0.4f * k);
    }
}

void Game::drawEffects() {
    for (const Pop& p : pops) {
        float k = p.t / 0.3f;
        auto alpha = static_cast<std::uint8_t>(255.f * (1.f - k));
        drawBall(p.color, p.pos, p.angle, 0, 1.f + 0.5f * k, sf::Color(255, 255, 255, alpha));
    }

    sf::RenderStates add(sf::BlendAdd);
    for (const Spark& s : sparks) {
        float t = s.t - s.delay;
        if (t < 0.f) continue;
        int frame = std::min(SPARK_FRAMES - 1, static_cast<int>(t / 0.5f * SPARK_FRAMES));
        sf::Sprite spr(texSpark, sf::IntRect({frame * SPARK_SIZE, 0}, {SPARK_SIZE, SPARK_SIZE}));
        spr.setOrigin({SPARK_SIZE / 2.f, SPARK_SIZE / 2.f});
        spr.setPosition(s.pos);
        spr.setScale({s.scale, s.scale});
        window.draw(spr, add);
    }

    for (const FloatText& t : texts) {
        float k = t.t / t.life;
        auto alpha = static_cast<std::uint8_t>(255.f * std::clamp((1.f - k) * 3.f, 0.f, 1.f));
        sf::Color c = t.color;
        c.a = alpha;
        drawText(t.text, t.pos + sf::Vector2f(0.f, -30.f * k), t.size, c, 0.5f, 2.f, sf::Color(0, 0, 0, alpha));
    }
}

void Game::drawText(const std::string& str, sf::Vector2f pos, unsigned size, sf::Color color, float alignX,
                    float outline, sf::Color outlineColor) {
    // Renderiza no dobro do tamanho e reduz: fica nítido com a janela ampliada.
    sf::Text text(font, utf8(str), size * 2);
    text.setFillColor(color);
    text.setOutlineThickness(outline * 2.f);
    text.setOutlineColor(outlineColor);
    sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin({bounds.position.x + bounds.size.x * alignX, bounds.position.y + bounds.size.y * 0.5f});
    text.setScale({0.5f, 0.5f});
    text.setPosition(pos);
    window.draw(text);
}

void Game::drawHud() {
    sf::RectangleShape bar({SCREEN_W, 22.f});
    bar.setFillColor(sf::Color(10, 12, 8, 170));
    window.draw(bar);

    drawText("FASE " + std::to_string(level), {8.f, 11.f}, 12, sf::Color(240, 220, 150), 0.f, 1.5f);

    // Vidas (sapinhos).
    for (int i = 0; i < std::min(lives, 8); ++i) {
        sf::Sprite icon(texFrog, FROG_RECT);
        icon.setScale({0.15f, 0.15f});
        icon.setPosition({62.f + 18.f * static_cast<float>(i), 3.f});
        window.draw(icon);
    }

    // Barra do Zuma.
    const float bx = 220.f, by = 6.f, bw = 200.f, bh = 10.f;
    float fill = std::clamp(static_cast<float>(score - levelStartScore) / static_cast<float>(target), 0.f, 1.f);
    sf::RectangleShape frame({bw, bh});
    frame.setPosition({bx, by});
    frame.setFillColor(sf::Color(30, 25, 15));
    frame.setOutlineThickness(1.f);
    frame.setOutlineColor(sf::Color(150, 130, 80));
    window.draw(frame);
    sf::RectangleShape progress({bw * fill, bh});
    progress.setPosition({bx, by});
    bool full = fill >= 1.f;
    float pulse = 0.5f + 0.5f * std::sin(clockTime * 8.f);
    progress.setFillColor(full ? sf::Color(255, static_cast<std::uint8_t>(200.f + 55.f * pulse), 60)
                               : sf::Color(230, 170, 40));
    window.draw(progress);
    if (full) drawText("ZUMA!", {bx + bw / 2.f, by + bh / 2.f}, 10, sf::Color::White, 0.5f, 1.5f);

    drawText(std::to_string(score), {SCREEN_W - 8.f, 11.f}, 13, sf::Color(255, 235, 170), 1.f, 1.5f);

    // Poderes ativos.
    std::string active;
    if (slowT > 0.f) active += "DEVAGAR  ";
    if (reverseT > 0.f) active += "RÉ  ";
    if (accuracyT > 0.f) active += "PRECISÃO";
    if (!active.empty()) drawText(active, {SCREEN_W / 2.f, 32.f}, 10, sf::Color(200, 240, 255), 0.5f, 1.5f);
}

void Game::drawDarken(std::uint8_t alpha) {
    sf::RectangleShape r({SCREEN_W, SCREEN_H});
    r.setFillColor(sf::Color(0, 0, 0, alpha));
    window.draw(r);
}

void Game::drawOverlay() {
    const sf::Color gold(255, 205, 70);
    const sf::Color light(240, 230, 200);
    bool blink = std::fmod(clockTime, 1.f) < 0.65f;

    switch (state) {
    case State::Title: {
        drawDarken(120);
        sf::Sprite god(texFrog, GOD_RECT);
        god.setOrigin({GOD_RECT.size.x / 2.f, GOD_RECT.size.y / 2.f});
        god.setPosition({SCREEN_W / 2.f, 220.f});
        god.setScale({1.25f, 1.25f});
        window.draw(god);
        drawText("ZUMA", {SCREEN_W / 2.f, 70.f}, 64, gold, 0.5f, 4.f);
        if (blink) drawText("Clique para jogar", {SCREEN_W / 2.f, 350.f}, 20, light);
        drawText("Mouse: mirar   Clique esquerdo: atirar   Clique direito / Espaço: trocar bola",
                 {SCREEN_W / 2.f, 420.f}, 10, light, 0.5f, 1.5f);
        drawText("P / Esc: pausar", {SCREEN_W / 2.f, 438.f}, 10, light, 0.5f, 1.5f);
        break;
    }
    case State::Playing:
        if (stateTime < 2.5f) {
            float a = std::clamp((2.5f - stateTime) * 2.f, 0.f, 1.f);
            auto alpha = static_cast<std::uint8_t>(255.f * a);
            drawText("FASE " + std::to_string(level), {SCREEN_W / 2.f, 200.f}, 36,
                     sf::Color(gold.r, gold.g, gold.b, alpha), 0.5f, 3.f, sf::Color(20, 12, 5, alpha));
        }
        break;
    case State::Paused:
        drawDarken(140);
        drawText("PAUSADO", {SCREEN_W / 2.f, 200.f}, 36, gold, 0.5f, 3.f);
        drawText("Clique ou pressione P para continuar", {SCREEN_W / 2.f, 250.f}, 14, light);
        break;
    case State::Losing:
        break;
    case State::LevelDone:
        drawDarken(120);
        drawText("FASE " + std::to_string(level) + " CONCLUÍDA!", {SCREEN_W / 2.f, 190.f}, 30, gold, 0.5f, 3.f);
        drawText("Pontos: " + std::to_string(score), {SCREEN_W / 2.f, 240.f}, 18, light);
        if (stateTime > 1.f && blink) drawText("Clique para continuar", {SCREEN_W / 2.f, 290.f}, 16, light);
        break;
    case State::GameOver: {
        drawDarken(160);
        sf::Sprite god(texFrog, GOD_RECT);
        god.setOrigin({GOD_RECT.size.x / 2.f, GOD_RECT.size.y / 2.f});
        god.setPosition({SCREEN_W / 2.f, 170.f});
        god.setColor(sf::Color(200, 120, 120));
        window.draw(god);
        drawText("FIM DE JOGO", {SCREEN_W / 2.f, 280.f}, 36, sf::Color(230, 70, 50), 0.5f, 3.f);
        drawText("Pontuação final: " + std::to_string(score) + "   (fase " + std::to_string(level) + ")",
                 {SCREEN_W / 2.f, 325.f}, 16, light);
        if (stateTime > 1.f && blink) drawText("Clique para voltar ao menu", {SCREEN_W / 2.f, 365.f}, 14, light);
        break;
    }
    }
}

}  // namespace

int main() {
    Game game;
    if (!game.init()) return 1;
    game.run();
    return 0;
}
