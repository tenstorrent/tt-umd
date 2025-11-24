FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
ENV OS_CODENAME=focal

COPY docker_install_common.sh /docker_install_common.sh
RUN chmod +x /docker_install_common.sh && /docker_install_common.sh
