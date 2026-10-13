CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LIBS = -lsfml-graphics -lsfml-window -lsfml-audio -lsfml-system
TARGET = jogo

$(TARGET): main.cpp estruturas.hpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean
