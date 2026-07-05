CloudTtsPipeline::CloudTtsPipeline() = default;

CloudTtsPipeline::~CloudTtsPipeline() {
    JoinWorker();
}
