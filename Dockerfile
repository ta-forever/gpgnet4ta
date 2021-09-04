FROM taforever/ci-ubuntu-focal:latest
WORKDIR /application
COPY build-ubuntu-focal-x64/bin/ ./
COPY replayserver.sh ./
COPY democompiler.sh ./
RUN chmod +x replayserver replayserver.sh democompiler.sh
