#include <SFML/Graphics.hpp>
#include <iostream>
#include <cmath>
#include <random>
#include <vector>

using namespace sf;

constexpr int BALL_FRAME_SIZE = 32;
constexpr int BALL_COLOR_COUNT = 6;
constexpr int BALL_FRAME_COUNT = 51;
constexpr float BALL_SCALE = 2.5f;
constexpr float BALL_RADIUS = (BALL_FRAME_SIZE * BALL_SCALE) / 2.f;
constexpr float PIXELS_PER_ROLL_FRAME = 6.f; 

struct Ball {
    Sprite sprite;
    Vector2f velocity;
    float distanceAccum = 0.f;

    Ball(const Texture& texture, int colorIndex)
        : sprite(texture) {
        sprite.setTextureRect(IntRect({colorIndex * BALL_FRAME_SIZE, 0},
                                           {BALL_FRAME_SIZE, BALL_FRAME_SIZE}));
        sprite.setOrigin({BALL_FRAME_SIZE / 2.f, BALL_FRAME_SIZE / 2.f});
        sprite.setScale({BALL_SCALE, BALL_SCALE});
    }

    void update(float deltaTime, Vector2u windowSize) {
        sprite.move(velocity * deltaTime);

        Vector2f pos = sprite.getPosition();

        if (pos.x - BALL_RADIUS < 0.f) {
            pos.x = BALL_RADIUS;
            velocity.x = -velocity.x;
        } else if (pos.x + BALL_RADIUS > windowSize.x) {
            pos.x = windowSize.x - BALL_RADIUS;
            velocity.x = -velocity.x;
        }

        if (pos.y - BALL_RADIUS < 0.f) {
            pos.y = BALL_RADIUS;
            velocity.y = -velocity.y;
        } else if (pos.y + BALL_RADIUS > windowSize.y) {
            pos.y = windowSize.y - BALL_RADIUS;
            velocity.y = -velocity.y;
        }

        sprite.setPosition(pos);

        // Avança o frame de rotação proporcionalmente à distância percorrida,
        // dando a impressão de que a bola realmente "rola" enquanto anda.
        float distanceThisFrame = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y) * deltaTime;
        distanceAccum += distanceThisFrame;

        int frame = static_cast<int>(distanceAccum / PIXELS_PER_ROLL_FRAME) % BALL_FRAME_COUNT;
        IntRect rect = sprite.getTextureRect();
        rect.position.y = frame * BALL_FRAME_SIZE;
        sprite.setTextureRect(rect);
    }
};

int main() {
    RenderWindow window(VideoMode({1280, 720}), "Teste SFML");

    Texture ballsTexture;
    ballsTexture.loadFromFile("./sprites/balls.png");

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_real_distribution<float> posX(BALL_RADIUS, 1280.f - BALL_RADIUS);
    std::uniform_real_distribution<float> posY(BALL_RADIUS, 720.f - BALL_RADIUS);
    std::uniform_real_distribution<float> angleDist(0.f, 2.f * 3.14159265f);
    std::uniform_real_distribution<float> speedDist(120.f, 260.f);

    std::vector<Ball> balls;
    for (int colorIndex = 0; colorIndex < BALL_COLOR_COUNT; ++colorIndex) {
        Ball ball(ballsTexture, colorIndex);
        ball.sprite.setPosition({posX(rng), posY(rng)});

        float angle = angleDist(rng);
        float speed = speedDist(rng);
        ball.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};

        balls.push_back(ball);
    }

    ConvexShape shape;
    shape.setPointCount(3);
    shape.setPoint(0, {0.f, -50.f});
    shape.setPoint(1, {-35.f, 40.f});
    shape.setPoint(2, {35.f, 40.f});
    shape.setOrigin({0.f, 10.f}); // centroide do triângulo
    shape.setPosition({640.f, 360.f});
    shape.setFillColor(Color::Cyan);

    CircleShape shape2(50.f);
    shape2.setFillColor(Color::Green);
    shape2.setPosition({375.f, 275.f});

    const float rotationSpeed = 180.f; // graus por segundo
    float speed = 200.f;
    Clock clock;

    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<Event::Closed>())
                window.close();
        }

        float deltaTime = clock.restart().asSeconds();

        if (Keyboard::isKeyPressed(Keyboard::Key::Left))
            shape.rotate(degrees(-rotationSpeed * deltaTime));
        if (Keyboard::isKeyPressed(Keyboard::Key::Right))
            shape.rotate(degrees(rotationSpeed * deltaTime));

        float angle = shape.getRotation().asRadians();
        Vector2f direction(std::sin(angle), -std::cos(angle));

        if (Keyboard::isKeyPressed(Keyboard::Key::Up))
            shape.move(direction * speed * deltaTime);
        if (Keyboard::isKeyPressed(Keyboard::Key::Down))
            shape.move(-direction * speed * deltaTime);

        for (Ball& ball : balls)
            ball.update(deltaTime, window.getSize());

        window.clear();
        window.draw(shape);
        for (const Ball& ball : balls)
            window.draw(ball.sprite);
        window.display();
    }

    return 0;
}
