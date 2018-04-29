# Builder container
FROM registry.cn-hangzhou.aliyuncs.com/tianchi4-docker/tianchi4-services AS builder

COPY sources.list /etc/apt

RUN apt-get update
RUN apt-get install -y build-essential libcurl4-openssl-dev libjansson-dev
#RUN apt-get install -y build-essential libcurl4-openssl-dev libjansson-dev libgoogle-perftools-dev

COPY . /root/workspace/agent/
WORKDIR /root/workspace/agent
RUN set -ex
RUN make clean && make

# Runner container
FROM registry.cn-hangzhou.aliyuncs.com/tianchi4-docker/debian-jdk8

RUN apt-get update
#RUN apt-get install -y net-tools nc
RUN apt-get install -y libcurl4-openssl-dev libjansson-dev
#RUN apt-get install -y libcurl4-openssl-dev libjansson-dev libgoogle-perftools-dev

#ENV TZ=Asia/Shanghai
#RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

COPY --from=builder /root/workspace/services/mesh-provider/target/mesh-provider-1.0-SNAPSHOT.jar /root/dists/mesh-provider.jar
COPY --from=builder /root/workspace/services/mesh-consumer/target/mesh-consumer-1.0-SNAPSHOT.jar /root/dists/mesh-consumer.jar
COPY --from=builder /root/workspace/agent/out/mesh-agent /root/dists/mesh-agent

COPY --from=builder /usr/local/bin/docker-entrypoint.sh /usr/local/bin
COPY start-agent.sh /usr/local/bin

RUN set -ex && mkdir -p /root/logs

ENTRYPOINT ["docker-entrypoint.sh"]
