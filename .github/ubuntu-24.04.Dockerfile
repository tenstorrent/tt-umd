FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV OS_CODENAME=noble

COPY docker_install_common.sh /docker_install_common.sh
RUN chmod +x /docker_install_common.sh && /docker_install_common.sh

